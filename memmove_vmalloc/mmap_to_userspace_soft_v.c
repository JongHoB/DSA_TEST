// This code is modified by Jongho Baik and the original code is from Linux Kernel Lab.

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include "mmap_to_userspace.h"

int main(int argc, const char **argv)
{
	struct timespec start, end;
	double s, e;
	int fd, test, fd2;
	char *malloc, *dst;
	int len = NPAGES * getpagesize();
	int i;
	int rc;

	system("mknod " MMAP_VDEV " c 42 0");
	system("mknod " MMAP_VDST_DEV " c 42 1");

	fd = open(MMAP_VDEV, O_RDWR | O_SYNC);
	if (fd < 0)
	{
		perror("open");
		system("rm " MMAP_VDEV);
		exit(EXIT_FAILURE);
	}

	fd2 = open(MMAP_VDST_DEV, O_RDWR | O_SYNC);
	if (fd2 < 0)
	{
		perror("open");
		system("rm " MMAP_VDST_DEV);
		system("rm " MMAP_VDEV);
		exit(EXIT_FAILURE);
	}

	malloc = (char *)mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (malloc == MAP_FAILED)
	{
		perror("mmap");
		system("rm " MMAP_VDEV);
		exit(EXIT_FAILURE);
	}

	dst = (char *)mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
	if (dst == MAP_FAILED)
	{
		perror("mmap");
		system("rm " MMAP_VDST_DEV);
		system("rm " MMAP_VDEV);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < NPAGES * getpagesize(); i += getpagesize())
	{
		printf("virtual address src : %p\n",malloc+i);
		printf("%s\n", malloc + i);
		printf("%s\n", dst + i);
		printf("virtual address dst : %p\n",dst+i);
	}

	/////////////////////////
	clock_gettime(CLOCK_MONOTONIC, &start);
	///////
	memmove(dst, malloc, len);
	///////
	clock_gettime(CLOCK_MONOTONIC, &end);
	////////////////////////
	printf("%ld %ld\n", start.tv_sec, start.tv_nsec);
	printf("%ld %ld\n", end.tv_sec, end.tv_nsec);

	printf("vmalloc memmove time in soft: %ld\n", end.tv_nsec - start.tv_nsec);

	rc = memcmp(malloc, dst, len);
	rc ? printf("v memmove failed\n") : printf("v memmove successful\n");

	close(fd);
	close(fd2);

	munmap(malloc, len);
	munmap(dst, len);

	system("rm " MMAP_VDEV);
	system("rm " MMAP_VDST_DEV);

	printf("size dst: %d\n", len);

	return 0;
}
