#include "/usr/src/linux-6.8-rc2/drivers/dma/idxd/idxd.h"
#include <linux/scatterlist.h>

#define NPAGES (1 << 10)
#define PAGE_ORDER 8
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

static struct idxd_desc_list
{
	struct list_head list;
	struct idxd_desc *desc;
	int completion;
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

static struct idxd_desc *idxd_desc_dma_submit_memcpy_sg(struct dma_chan *c, struct sg_table *desc, struct sg_table *src, size_t sg_len, unsigned long flags)
{
	struct scatterlist *sg_desc;
	struct scatterlist *sg_src;
	int i;
	struct idxd_wq *wq = to_idxd_wq(c);
	u32 desc_flags;
	struct idxd_device *idxd = wq->idxd;
	struct idxd_desc *batch_desc;
	// struct idxd_desc **sub_descs = (struct idxd_desc **)kmalloc(sg_len * sizeof(struct idxd_desc *), GFP_KERNEL);
	struct dsa_hw_desc *sub_hw_descs = (struct dsa_hw_desc *)kmalloc(sg_len * sizeof(struct dsa_hw_desc), GFP_KERNEL);
	struct dsa_completion_record *sub_compl = (struct dsa_completion_record *)kmalloc(sg_len * sizeof(struct dsa_completion_record), GFP_KERNEL);

	if (wq->state != IDXD_WQ_ENABLED)
		return NULL;

	if (sg_len > idxd->max_batch_size)
		return NULL;

	op_flag_setup(flags, &desc_flags);
	batch_desc = idxd_alloc_desc(wq, IDXD_OP_BLOCK);
	if (IS_ERR(batch_desc))
		return NULL;

	idxd_prep_desc_batch(wq, batch_desc->hw, DSA_OPCODE_BATCH, (uint64_t)&sub_hw_descs[0], sg_len, batch_desc->compl_dma, desc_flags);

	pr_info("sg_len: %d\n", sg_len);

	batch_desc->txd.flags = flags;

	for_each_2_sgtable_dsa_sg(desc, src, sg_desc, sg_src, sg_len, i)
	{
		pr_info("i: %d\n", i);
		pr_info("tx_size: %d\n", sg_dma_len(sg_desc));

		memset(&sub_hw_descs[i], 0, sizeof(struct dsa_hw_desc));
		memset(&sub_compl[i], 0, sizeof(struct dsa_completion_record));

		sub_compl[i].status = 0;
		sub_hw_descs[i].opcode = DSA_OPCODE_MEMMOVE;
		sub_hw_descs[i].src_addr = sg_dma_address(sg_src);
		sub_hw_descs[i].dst_addr = sg_dma_address(sg_desc);
		sub_hw_descs[i].xfer_size = sg_dma_len(sg_desc);
		sub_hw_descs[i].flags = desc_flags;
		sub_hw_descs[i].priv = 0;
		sub_hw_descs[i].completion_addr = (uintptr_t)&sub_compl[i];

		if (device_pasid_enabled(idxd))
		{
			sub_hw_descs[i].pasid = idxd->pasid;
		}
	}

	return batch_desc;
}
