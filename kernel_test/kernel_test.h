#include "/usr/src/linux-6.8-rc2/drivers/dma/idxd/idxd.h"
#define NPAGES 1
#define PAGE_ORDER 0
#define DSA_LIST 8
#define DSA_NUM 2
#define WQ_NUM 1

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
