#include "/usr/src/linux-6.8-rc2/drivers/dma/idxd/idxd.h"
#include <linux/scatterlist.h>

#define NPAGES (1 << 11)
#define PAGE_ORDER 8
#define DSA_LIST 8
#define DSA_NUM 2
#define WQ_NUM 1

#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 10
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

#define ENQ_RETRY_MAX 1000
#define POLL_RETRY_MAX 1000000
#define FAULT_RETRY_MAX 10000

#define for_each_2_sg(sglist1, sglist2, sg1, sg2, len, __i) \
	for (__i = 0, sg1 = (sglist1), sg2 = (sglist2); __i < (len); __i++, sg1 = sg_next(sg1), sg2 = sg_next(sg2))

#define for_each_2_sgtable_dsa_sg(sgt1, sgt2, sg1, sg2, len, i) for_each_2_sg((sgt1)->sgl, (sgt2)->sgl, sg1, sg2, len, i)

#define idxd_confdev(idxd) &idxd->idxd_dev.conf_dev
#define wq_confdev(wq) &wq->idxd_dev.conf_dev
#define engine_confdev(engine) &engine->idxd_dev.conf_dev
#define group_confdev(group) &group->idxd_dev.conf_dev
#define cdev_dev(cdev) &cdev->idxd_dev.conf_dev
#define user_ctx_dev(ctx) (&(ctx)->idxd_dev.conf_dev)

#define confdev_to_idxd_dev(dev) container_of(dev, struct idxd_dev, conf_dev)
#define idxd_dev_to_idxd(idxd_dev) container_of(idxd_dev, struct idxd_device, idxd_dev)
#define idxd_dev_to_wq(idxd_dev) container_of(idxd_dev, struct idxd_wq, idxd_dev)

#define wq_to_dev(wq) &wq->idxd->pdev->dev

struct enabled_idxd_wqs
{
	struct list_head list;
	// struct device *dev;
	struct idxd_wq *wq;
	// struct dma_chan *chan;
	int enabled_wq_num;
};

struct batch_info
{
	struct dsa_completion_record *sub_compl;
	struct dsa_hw_desc *sub_hw_descs;
	dma_addr_t compls_addr;
	dma_addr_t hw_descs_addr;
	int batch_compls_size;
	int batch_hw_descs_size;
};
struct idxd_desc_list
{
	struct list_head list;
	struct idxd_desc *desc;
	int completion;

	struct batch_info *batch_info;
};

struct batch_task
{
	struct scatterlist *next_sg_src;
	struct scatterlist *next_sg_dst;
	struct idxd_desc_list *desc_list;
};

struct dma_chan *
dma_get_slave_channel(struct dma_chan *chan);

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

static inline void idxd_prep_desc_batch(struct idxd_wq *wq, struct dsa_hw_desc *hw, char opcode, u64 sub_descs_list_addr, u32 desc_count, u64 compl, u32 flags)
{
	hw->flags = flags;
	hw->opcode = opcode;
	hw->desc_list_addr = sub_descs_list_addr;
	hw->desc_count = desc_count;

	hw->priv = 0;
	hw->completion_addr = compl ;
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

static inline struct idxd_desc *
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

static inline struct batch_task *idxd_desc_dma_submit_memcpy_sg(struct dma_chan *c, struct scatterlist *desc, struct scatterlist *src, int ents, unsigned long flags)
{
	struct scatterlist *sg_desc;
	struct scatterlist *sg_src;
	int i;

	struct idxd_wq *wq = to_idxd_wq(c);
	u32 desc_flags;
	struct idxd_device *idxd = wq->idxd;
	struct idxd_desc *batch_desc;

	struct dsa_completion_record *sub_compl;
	struct dsa_hw_desc *sub_hw_descs;

	struct batch_task *task = (struct batch_task *)kzalloc(sizeof(struct batch_task), GFP_KERNEL);
	task->desc_list = (struct idxd_desc_list *)kzalloc(sizeof(struct idxd_desc_list), GFP_KERNEL);
	task->desc_list->batch_info = (struct batch_info *)kzalloc(sizeof(struct batch_info), GFP_KERNEL);

	if (wq->state != IDXD_WQ_ENABLED)
		return NULL;
	// pr_info("5\n");
	op_flag_setup(flags, &desc_flags);
	batch_desc = idxd_alloc_desc(wq, IDXD_OP_BLOCK);
	if (IS_ERR(batch_desc))
		return NULL;
	// pr_info("6\n");
	task->desc_list->batch_info->batch_compls_size = ents * idxd->data->compl_size;
	task->desc_list->batch_info->batch_hw_descs_size = ents * sizeof(struct dsa_hw_desc);
	// pr_info("7\n");
	sub_compl = kzalloc(task->desc_list->batch_info->batch_compls_size, GFP_KERNEL);
	task->desc_list->batch_info->compls_addr = dma_map_single(&idxd->pdev->dev, sub_compl, task->desc_list->batch_info->batch_compls_size, DMA_BIDIRECTIONAL);
	// pr_info("8\n");
	sub_hw_descs = kzalloc(task->desc_list->batch_info->batch_hw_descs_size, GFP_KERNEL);
	task->desc_list->batch_info->hw_descs_addr = dma_map_single(&idxd->pdev->dev, sub_hw_descs, task->desc_list->batch_info->batch_hw_descs_size, DMA_TO_DEVICE);
	// pr_info("9\n");

	task->desc_list->batch_info->sub_compl = sub_compl;
	task->desc_list->batch_info->sub_hw_descs = sub_hw_descs;

	idxd_prep_desc_batch(wq, batch_desc->hw, DSA_OPCODE_BATCH, task->desc_list->batch_info->hw_descs_addr, ents, batch_desc->compl_dma, desc_flags);

	batch_desc->txd.flags = flags;

	for_each_2_sg(desc, src, sg_desc, sg_src, ents, i)
	{
		// pr_info("i: %d\n", i);
		// pr_info("tx_size: %d\n", sg_dma_len(sg_desc));

		sub_compl[i].status = 0;
		sub_hw_descs[i].opcode = DSA_OPCODE_MEMMOVE;
		sub_hw_descs[i].src_addr = sg_dma_address(sg_src);
		sub_hw_descs[i].dst_addr = sg_dma_address(sg_desc);
		sub_hw_descs[i].xfer_size = sg_dma_len(sg_desc);
		sub_hw_descs[i].flags = desc_flags;
		sub_hw_descs[i].priv = 0;
		sub_hw_descs[i].completion_addr = task->desc_list->batch_info->compls_addr + i * idxd->data->compl_size;

		if (device_pasid_enabled(idxd))
		{
			sub_hw_descs[i].pasid = idxd->pasid;
		}
	}

	task->next_sg_src = sg_src;
	task->next_sg_dst = sg_desc;
	task->desc_list->desc = batch_desc;
	task->desc_list->completion = 0;

	return task;
}
