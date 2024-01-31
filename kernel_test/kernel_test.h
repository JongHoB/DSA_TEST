
#define NPAGES 512

#ifndef __SO2MMAP_H__
#define __SO2MMAP_H__ 1

#define PROC_VMALLOC_NAME "my-vmalloc-entry"
#define PROC_KMALLOC_NAME "my-kmalloc-entry"

#define MMAP_VDEV "/dev/myvmap"
#define MMAP_KDEV "/dev/mykmap"

#define virt_to_pfn(kaddr) \
	((unsigned long)(kaddr) >> PAGE_SHIFT)
#endif

#define WQ_PORTAL_SIZE 4096

#define ENQ_RETRY_MAX 1000
#define POLL_RETRY_MAX 10000

#define UMWAIT_DELAY 100000

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
