
#define NPAGES 1024

#ifndef __SO2MMAP_H__
#define __SO2MMAP_H__ 1

#define PROC_VMALLOC_NAME "my-vmalloc-entry"
#define PROC_KMALLOC_NAME "my-kmalloc-entry"

#define MMAP_VDEV "/dev/myvmap"
#define MMAP_KDEV "/dev/mykmap"

#define virt_to_pfn(kaddr) \
	((unsigned long)(kaddr) >> PAGE_SHIFT)
#endif

// #define WQ_PORTAL_SIZE 4096

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

// static inline struct idxd_device_driver *wq_to_idxd_drv(struct idxd_wq *wq)
// {
// 	struct device *dev = wq_confdev(wq);
// 	struct idxd_device_driver *idxd_drv =
// 		container_of(dev->driver, struct idxd_device_driver, drv);

// 	return idxd_drv;
// }

// static inline unsigned char enqcmd(void *dst, const void *src)
// {
// 	unsigned char retry;

// 	asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
// 			"setz %0\t\n"
// 			: "=r"(retry) : "a" (dst), "d" (src));
// 	return retry;
// }

// static inline void movdir64b(void *dst, const void *src)
// {
// 	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02\t\n"
// 		: : "a" (dst), "d" (src));
// }

// static __always_inline int umwait(unsigned long timeout, unsigned int state)
// {
// 	uint8_t r;
// 	uint32_t timeout_low = (uint32_t)timeout;
// 	uint32_t timeout_high = (uint32_t)(timeout >> 32);

// 	asm volatile(".byte 0xf2, 0x48, 0x0f, 0xae, 0xf1\t\n"
// 				 "setc %0\t\n"
// 				 : "=r"(r)
// 				 : "c"(state), "a"(timeout_low), "d"(timeout_high));
// 	return r;
// }
