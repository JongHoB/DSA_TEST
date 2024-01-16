// This code is modified by Jongho Baik and the original code is from Linux Kernel Lab.
/*
 * PSO - Memory Mapping Lab (#11)
 *
 * Exercise #1, #2: memory mapping between user-space and kernel-space
 *
 * test case
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include "mmap_to_userspace.h"
#include <linux/idxd.h>
#include <accel-config/libaccel_config.h>

#include <x86intrin.h>

#define MMAP_DEV "/dev/mymmap"
#define PROC_ENTRY_PATH "/proc/" PROC_ENTRY_NAME

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

static int show_mem_usage(void)
{
	int fd, ret;
	char buf[40];
	unsigned long mem_usage;

	fd = open(PROC_ENTRY_PATH, O_RDONLY);
	if (fd < 0)
	{
		perror("open " PROC_ENTRY_PATH);
		ret = fd;
		goto out;
	}

	ret = read(fd, buf, sizeof buf);
	if (ret < 0)
		goto no_read;

	sscanf(buf, "%lu", &mem_usage);
	buf[ret] = 0;

	printf("Memory usage: %lu\n", mem_usage);

	ret = mem_usage;
no_read:
	close(fd);
out:
	return ret;
}

int main(int argc, const char **argv)
{
	struct timeval start, end;
	double s, e;
	int fd, test;
	char *addr;
	int len = NPAGES * getpagesize();
	int i;
	unsigned long usage_mmap;

	char dst[len];

	system("mknod " MMAP_DEV " c 42 0");

	fd = open(MMAP_DEV, O_RDWR | O_SYNC);
	if (fd < 0)
	{
		perror("open");
		system("rm " MMAP_DEV);
		exit(EXIT_FAILURE);
	}

	addr = (char *)mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
	{
		perror("mmap");
		system("rm " MMAP_DEV);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < NPAGES * getpagesize(); i += getpagesize())
	{
		// In each page, sprintf(vmalloc_area + i, "Hello World %d", i / PAGE_SIZE); is executed.
		// So, in each page, "Hello World %d" is written.
		// print the content of each page
		printf("%s\n", addr + i);
	}

	// usage_mmap = show_mem_usage();
	// if (usage_mmap < 0)
	// 	printf("failed to show memory usage\n");

	void *wq_portal;
	struct dsa_hw_desc desc = {};
	struct dsa_completion_record comp __attribute__((aligned(32)));
	int rc;
	int poll_retry, enq_retry;
	wq_portal = map_wq(len);
	if (wq_portal == MAP_FAILED)
		return EXIT_FAILURE;
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
	desc.xfer_size = len;
	desc.src_addr = (uintptr_t)addr;
	desc.dst_addr = (uintptr_t)dst;
	desc.completion_addr = (uintptr_t)&comp;
retry:
	comp.status = 0;
	/* Ensure previous writes are ordered with respect to ENQCMD */
	_mm_sfence();
	enq_retry = 0;
	//////////////////////////////////////////////////////////////
	gettimeofday(&start, NULL);
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
	gettimeofday(&end, NULL);
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
		rc = memcmp(addr, dst, len);
		rc ? printf("memmove failed\n") : printf("memmove successful\n");
		rc = rc ? EXIT_FAILURE : EXIT_SUCCESS;
	}
done:
	munmap(wq_portal, WQ_PORTAL_SIZE);

	printf("%ld %ld\n", start.tv_sec, start.tv_usec);
	printf("%ld %ld\n", end.tv_sec, end.tv_usec);

	printf("memmove time in soft: %ld\n", end.tv_usec - start.tv_usec);
	printf("size dst: %d\n", len);

	close(fd);

	system("rm " MMAP_DEV);

	return 0;
}
