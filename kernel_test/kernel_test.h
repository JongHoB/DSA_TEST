#include "/usr/src/linux-6.8-rc2/drivers/dma/idxd/idxd.h"
#define NPAGES 1
#define PAGE_ORDER 7
#define DSA_LIST 8
#define DSA_NUM 2
#define WQ_NUM 1

#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 10
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

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
#define FAULT_RETRY_MAX 10000

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

static inline struct idxd_wq *to_idxd_wq(struct dma_chan *c)
{
	struct idxd_dma_chan *idxd_chan;

	idxd_chan = container_of(c, struct idxd_dma_chan, chan);
	return idxd_chan->wq;
}

static void op_flag_setup(unsigned long flags, u32 *desc_flags)
{
	*desc_flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (flags & DMA_PREP_INTERRUPT)
		*desc_flags |= IDXD_OP_FLAG_RCI;
}

static inline void idxd_prep_desc_common(struct idxd_wq *wq,
										 struct dsa_hw_desc *hw, char opcode,
										 u64 addr_f1, u64 addr_f2, u64 len,
										 u64 compl, u32 flags)
{
	hw->flags = flags;
	hw->opcode = opcode;
	hw->src_addr = addr_f1;
	hw->dst_addr = addr_f2;
	hw->xfer_size = len;
	/*
	 * For dedicated WQ, this field is ignored and HW will use the WQCFG.priv
	 * field instead. This field should be set to 0 for kernel descriptors
	 * since kernel DMA on VT-d supports "user" privilege only.
	 */
	hw->priv = 0;
	hw->completion_addr = compl ;
}

static struct idxd_desc *
idxd_desc_dma_submit_memcpy(struct dma_chan *c, dma_addr_t dma_dest,
							dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct idxd_wq *wq = to_idxd_wq(c);
	u32 desc_flags;
	struct idxd_device *idxd = wq->idxd;
	struct idxd_desc *desc;

	if (wq->state != IDXD_WQ_ENABLED)
		return NULL;

	if (len > idxd->max_xfer_bytes)
		return NULL;

	op_flag_setup(flags, &desc_flags);
	desc = idxd_alloc_desc(wq, IDXD_OP_BLOCK);
	if (IS_ERR(desc))
		return NULL;

	idxd_prep_desc_common(wq, desc->hw, DSA_OPCODE_MEMMOVE,
						  dma_src, dma_dest, len, desc->compl_dma,
						  desc_flags);

	desc->txd.flags = flags;

	return desc;
}
