#include "/usr/src/linux-6.8-rc2/drivers/dma/idxd/idxd.h"
#define NPAGES 1

#ifndef __SO2MMAP_H__
#define __SO2MMAP_H__ 1

#define PROC_VMALLOC_NAME "my-vmalloc-entry"
#define PROC_KMALLOC_NAME "my-kmalloc-entry"

#define MMAP_VDEV "/dev/myvmap"
#define MMAP_KDEV "/dev/mykmap"

#define virt_to_pfn(kaddr) \
	((unsigned long)(kaddr) >> PAGE_SHIFT)
#endif

#define ENQ_RETRY_MAX 1000
#define POLL_RETRY_MAX 10000

#define idxd_confdev(idxd) &idxd->idxd_dev.conf_dev
#define wq_confdev(wq) &wq->idxd_dev.conf_dev
#define engine_confdev(engine) &engine->idxd_dev.conf_dev
#define group_confdev(group) &group->idxd_dev.conf_dev
#define cdev_dev(cdev) &cdev->idxd_dev.conf_dev
#define user_ctx_dev(ctx) (&(ctx)->idxd_dev.conf_dev)

#define confdev_to_idxd_dev(dev) container_of(dev, struct idxd_dev, conf_dev)
#define idxd_dev_to_idxd(idxd_dev) container_of(idxd_dev, struct idxd_device, idxd_dev)
#define idxd_dev_to_wq(idxd_dev) container_of(idxd_dev, struct idxd_wq, idxd_dev)

struct dma_chan *dma_get_slave_channel(struct dma_chan *chan);

static inline bool idxd_device_is_halted(struct idxd_device *idxd)
{
	union gensts_reg gensts;

	gensts.bits = ioread32(idxd->reg_base + IDXD_GENSTATS_OFFSET);

	return (gensts.state == IDXD_DEVICE_STATE_HALT);
}

static void idxd_cmd_exec(struct idxd_device *idxd, int cmd_code, u32 operand,
						  u32 *status)
{
	union idxd_command_reg cmd;
	DECLARE_COMPLETION_ONSTACK(done);
	u32 stat;
	unsigned long flags;

	if (idxd_device_is_halted(idxd))
	{
		dev_warn(&idxd->pdev->dev, "Device is HALTED!\n");
		if (status)
			*status = IDXD_CMDSTS_HW_ERR;
		return;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = cmd_code;
	cmd.operand = operand;
	cmd.int_req = 1;

	spin_lock_irqsave(&idxd->cmd_lock, flags);
	wait_event_lock_irq(idxd->cmd_waitq,
						!test_bit(IDXD_FLAG_CMD_RUNNING, &idxd->flags),
						idxd->cmd_lock);

	dev_dbg(&idxd->pdev->dev, "%s: sending cmd: %#x op: %#x\n",
			__func__, cmd_code, operand);

	idxd->cmd_status = 0;
	__set_bit(IDXD_FLAG_CMD_RUNNING, &idxd->flags);
	idxd->cmd_done = &done;
	iowrite32(cmd.bits, idxd->reg_base + IDXD_CMD_OFFSET);

	/*
	 * After command submitted, release lock and go to sleep until
	 * the command completes via interrupt.
	 */
	spin_unlock_irqrestore(&idxd->cmd_lock, flags);
	wait_for_completion(&done);
	stat = ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET);
	spin_lock(&idxd->cmd_lock);
	if (status)
		*status = stat;
	idxd->cmd_status = stat & GENMASK(7, 0);

	__clear_bit(IDXD_FLAG_CMD_RUNNING, &idxd->flags);
	/* Wake up other pending commands */
	wake_up(&idxd->cmd_waitq);
	spin_unlock(&idxd->cmd_lock);
}

static void idxd_wq_disable_cleanup(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;

	lockdep_assert_held(&wq->wq_lock);
	wq->state = IDXD_WQ_DISABLED;
	memset(wq->wqcfg, 0, idxd->wqcfg_size);
	wq->type = IDXD_WQT_NONE;
	wq->threshold = 0;
	wq->priority = 0;
	wq->enqcmds_retries = IDXD_ENQCMDS_RETRIES;
	wq->flags = 0;
	memset(wq->name, 0, WQ_NAME_SIZE);
	wq->max_xfer_bytes = WQ_DEFAULT_MAX_XFER;
	idxd_wq_set_max_batch_size(idxd->data->type, wq, WQ_DEFAULT_MAX_BATCH);
	if (wq->opcap_bmap)
		bitmap_copy(wq->opcap_bmap, idxd->opcap_bmap, IDXD_MAX_OPCAP_BITS);
}

int idxd_wq_enable(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 status;

	if (wq->state == IDXD_WQ_ENABLED)
	{
		dev_dbg(dev, "WQ %d already enabled\n", wq->id);
		return 0;
	}

	idxd_cmd_exec(idxd, IDXD_CMD_ENABLE_WQ, wq->id, &status);

	if (status != IDXD_CMDSTS_SUCCESS &&
		status != IDXD_CMDSTS_ERR_WQ_ENABLED)
	{
		dev_dbg(dev, "WQ enable failed: %#x\n", status);
		return -ENXIO;
	}

	wq->state = IDXD_WQ_ENABLED;
	set_bit(wq->id, idxd->wq_enable_map);
	dev_dbg(dev, "WQ %d enabled\n", wq->id);
	return 0;
}

int idxd_wq_disable(struct idxd_wq *wq, bool reset_config)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 status, operand;

	dev_dbg(dev, "Disabling WQ %d\n", wq->id);

	if (wq->state != IDXD_WQ_ENABLED)
	{
		dev_dbg(dev, "WQ %d in wrong state: %d\n", wq->id, wq->state);
		return 0;
	}

	operand = BIT(wq->id % 16) | ((wq->id / 16) << 16);
	idxd_cmd_exec(idxd, IDXD_CMD_DISABLE_WQ, operand, &status);

	if (status != IDXD_CMDSTS_SUCCESS)
	{
		dev_dbg(dev, "WQ disable failed: %#x\n", status);
		return -ENXIO;
	}

	if (reset_config)
		idxd_wq_disable_cleanup(wq);
	clear_bit(wq->id, idxd->wq_enable_map);
	wq->state = IDXD_WQ_DISABLED;
	dev_dbg(dev, "WQ %d disabled\n", wq->id);
	return 0;
}

static void __idxd_wq_set_pasid_locked(struct idxd_wq *wq, int pasid)
{
	struct idxd_device *idxd = wq->idxd;
	union wqcfg wqcfg;
	unsigned int offset;

	offset = WQCFG_OFFSET(idxd, wq->id, WQCFG_PASID_IDX);
	spin_lock(&idxd->dev_lock);
	wqcfg.bits[WQCFG_PASID_IDX] = ioread32(idxd->reg_base + offset);
	wqcfg.pasid_en = 1;
	wqcfg.pasid = pasid;
	wq->wqcfg->bits[WQCFG_PASID_IDX] = wqcfg.bits[WQCFG_PASID_IDX];
	iowrite32(wqcfg.bits[WQCFG_PASID_IDX], idxd->reg_base + offset);
	spin_unlock(&idxd->dev_lock);
}

int idxd_wq_set_pasid(struct idxd_wq *wq, int pasid)
{
	int rc;

	rc = idxd_wq_disable(wq, false);

	if (rc < 0)
		return rc;

	__idxd_wq_set_pasid_locked(wq, pasid);

	rc = idxd_wq_enable(wq);
	if (rc < 0)
		return rc;

	return 0;
}
