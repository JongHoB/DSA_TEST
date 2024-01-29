#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/idxd.h>
#include <accel-config/libaccel_config.h>
#include <x86intrin.h>


#define WQ_PORTAL_SIZE 4096

#define ENQ_RETRY_MAX 1000
#define POLL_RETRY_MAX 10000

#define UMWAIT_DELAY 100000

static inline unsigned char enqcmd(void *dst, const void *src)
{
	unsigned char retry;

	asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
			"setz %0\t\n"
			: "=r"(retry) : "a" (dst), "d" (src));
	return retry;
}

static inline void movdir64b(void *dst, const void *src)
{
	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02\t\n"
		: : "a" (dst), "d" (src));
}

static __always_inline int umwait(unsigned long timeout, unsigned int state)
{
	uint8_t r;
	uint32_t timeout_low = (uint32_t)timeout;
	uint32_t timeout_high = (uint32_t)(timeout >> 32);

	asm volatile(".byte 0xf2, 0x48, 0x0f, 0xae, 0xf1\t\n"
				 "setc %0\t\n"
				 : "=r"(r)
				 : "c"(state), "a"(timeout_low), "d"(timeout_high));
	return r;
}

static void *map_wq(int BLEN)
{
	void *wq_portal;
	struct accfg_ctx *ctx;
	struct accfg_wq *wq;
	struct accfg_device *device;
	char path[PATH_MAX];
	int fd;
	int wq_found;
	accfg_new(&ctx);
	accfg_device_foreach(ctx, device)
	{
		/* Use accfg_device_(*) functions to select enabled device
		 * based on name, numa node
		 * */
		accfg_wq_foreach(device, wq)
		{
			if (accfg_wq_get_user_dev_path(wq, path, sizeof(path)))
				continue;
			if (accfg_wq_get_max_transfer_size(wq) < BLEN)
				continue;
			/* Use accfg_wq_(*) functions select WQ of type
			 ** ACCFG_WQT_USER and desired mode
			 **/
			wq_found = accfg_wq_get_type(wq) == ACCFG_WQT_USER &&
					   accfg_wq_get_mode(wq) == ACCFG_WQ_SHARED;
			if (wq_found)
			{
				pr_info(KERN_INFO "wq user device file path: %s\n", accfg_wq_get_devname(wq));
				break;
			}
		}
		if (wq_found)
			break;
	}
	accfg_unref(ctx);
	if (!wq_found)
		return MAP_FAILED;
	fd = open(path, O_RDWR);
	if (fd < 0)
		return MAP_FAILED;
	wq_portal = mmap(NULL, WQ_PORTAL_SIZE, PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
	close(fd);
	return wq_portal;
}