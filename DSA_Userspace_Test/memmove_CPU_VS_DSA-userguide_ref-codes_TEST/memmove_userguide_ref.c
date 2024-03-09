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
#include <time.h>
#include <x86intrin.h>

#include "memmove.h"

#define WQ_PORTAL_SIZE 4096

#define ENQ_RETRY_MAX 1000
#define POLL_RETRY_MAX 10000

#define UMWAIT_DELAY 100000

static uint8_t op_status(uint8_t status)
{
	return status & DSA_COMP_STATUS_MASK;
}
static inline unsigned int enqcmd(void *dst, const void *src)
{
	uint8_t retry;
	asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
				 "setz %0\t\n"
				 : "=r"(retry) : "a"(dst), "d"(src));
	return (unsigned int)retry;
}
static __always_inline inline void umonitor(const volatile void *addr)
{
	asm volatile(".byte 0xf3, 0x48, 0x0f, 0xae, 0xf0" : : "a"(addr));
}

static __always_inline inline int umwait(unsigned long timeout, unsigned int state)
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
				printf("wq user device file path: %s\n", accfg_wq_get_devname(wq));
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

int main()
{

	struct timespec start, end;
	double s, e;

	void *wq_portal;
	struct dsa_hw_desc desc = {};

	int BLEN = NPAGES * getpagesize();

	char src[BLEN];
	char dst[BLEN];
	struct dsa_completion_record comp __attribute__((aligned(32)));
	int rc;
	int poll_retry, enq_retry;
	wq_portal = map_wq(BLEN);
	if (wq_portal == MAP_FAILED)
		return EXIT_FAILURE;

	for (int i = 0; i < BLEN; i += getpagesize())
	{
		switch (i % 10)
		{
		case 0:
			src[i] = 'a';
			break;
		case 1:
			src[i] = 'b';
			break;
		case 2:
			src[i] = 'c';
			break;
		case 3:
			src[i] = 'd';
			break;
		case 4:
			src[i] = 'e';
			break;
		case 5:
			src[i] = 'f';
			break;
		case 6:
			src[i] = 'g';
			break;
		case 7:
			src[i] = 'h';
			break;
		case 8:
			src[i] = 'i';
			break;
		case 9:
			src[i] = 'j';
			break;
		}
	}

	desc.opcode = DSA_OPCODE_MEMMOVE;
	/*
	 ** Request a completion – since we poll on status, this flag
	 ** must be 1 for status to be updated on successful
	 ** completion
	 **/
	desc.flags = IDXD_OP_FLAG_RCR;
	/* CRAV should be 1 since RCR = 1 */
	desc.flags |= IDXD_OP_FLAG_CRAV;
	/* Hint to direct data writes to CPU cache */
	desc.flags |= IDXD_OP_FLAG_CC;
	desc.xfer_size = BLEN;
	desc.src_addr = (uintptr_t)src;
	desc.dst_addr = (uintptr_t)dst;
	desc.completion_addr = (uintptr_t)&comp;
retry:
	comp.status = 0;
	/* Ensure previous writes are ordered with respect to ENQCMD */
	_mm_sfence();
	enq_retry = 0;
	//////////////////////////////////////////////////////////////
	clock_gettime(CLOCK_MONOTONIC, &start);
	/////

	while (enqcmd(wq_portal, &desc) && enq_retry++ < ENQ_RETRY_MAX)
		;

	if (enq_retry == ENQ_RETRY_MAX)
	{
		printf("ENQCMD retry limit exceeded\n");
		rc = EXIT_FAILURE;
		goto done;
	}
	poll_retry = 0;
	while (comp.status == 0 && poll_retry++ < POLL_RETRY_MAX)
	{
		//_mm_pause();
		umonitor(&comp);
		if (comp.status == 0)
		{
			uint64_t delay = __rdtsc() + UMWAIT_DELAY;
			umwait(delay, 1);
		}
	}
	/////
	clock_gettime(CLOCK_MONOTONIC, &end);
	/////////////////////////////////////////////////////////////
	if (poll_retry == POLL_RETRY_MAX)
	{
		printf("Completion status poll retry limit exceeded\n");
		rc = EXIT_FAILURE;
		goto done;
	}
	if (comp.status != DSA_COMP_SUCCESS)
	{
		if (op_status(comp.status) == DSA_COMP_PAGE_FAULT_NOBOF)
		{
			int wr = comp.status & DSA_COMP_STATUS_WRITE;
			volatile char *t;
			t = (char *)comp.fault_addr;
			wr ? *t = *t : *t;
			desc.src_addr += comp.bytes_completed;
			desc.dst_addr += comp.bytes_completed;
			desc.xfer_size -= comp.bytes_completed;
			goto retry;
		}
		else
		{
			printf("desc failed status %u\n", comp.status);
			rc = EXIT_FAILURE;
		}
	}
	else
	{
		printf("desc successful\n");
		rc = memcmp(src, dst, BLEN);
		rc ? printf("memmove failed\n") : printf("memmove successful\n");
		rc = rc ? EXIT_FAILURE : EXIT_SUCCESS;
	}
done:
	munmap(wq_portal, WQ_PORTAL_SIZE);

	///////////////////////////////////////////

	printf("memmove time in dsa: %ld\n", end.tv_nsec - start.tv_nsec);
	printf("size dst: %d\n", sizeof(dst) / sizeof(char));
	//////////////////////////////////////////
	return rc;
}
