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
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <string.h>
#include "mmap_to_userspace.h"

#define MMAP_DEV "/dev/mymmap"
#define PROC_ENTRY_PATH "/proc/" PROC_ENTRY_NAME

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

	/////////////////////////
	gettimeofday(&start, NULL);
	///////
	memmove(dst, addr, len);
	///////
	gettimeofday(&end, NULL);
	////////////////////////
	printf("%ld %ld\n", start.tv_sec, start.tv_usec);
	printf("%ld %ld\n", end.tv_sec, end.tv_usec);

	printf("memmove time in soft: %ld\n", end.tv_usec - start.tv_usec);
	printf("size dst: %d\n", len);

	int rc = memcmp(addr, dst, len);
	rc ? printf("memmove failed\n") : printf("memmove successful\n");

	close(fd);

	system("rm " MMAP_DEV);

	return 0;
}
