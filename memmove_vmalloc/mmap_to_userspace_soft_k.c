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
	int fd, test;
	char *malloc;
	int len = NPAGES * getpagesize();
	int i;
	int rc;

	char dst2[len];

	system("mknod " MMAP_KDEV " c 43 0");

	fd = open(MMAP_KDEV, O_RDWR | O_SYNC);
	if (fd < 0)
	{
		perror("open");
		system("rm " MMAP_KDEV);
		exit(EXIT_FAILURE);
	}

	malloc = (char *)mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (malloc == MAP_FAILED)
	{
		perror("mmap");
		system("rm " MMAP_KDEV);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < NPAGES * getpagesize(); i += getpagesize())
	{
		// In each page, sprintf(vmalloc_area + i, "Hello World %d", i / PAGE_SIZE); is executed.
		// So, in each page, "Hello World %d" is written.
		// print the content of each page
		printf("%s\n", malloc + i);
	}

	/////////////////////////
	clock_gettime(CLOCK_MONOTONIC, &start);
	///////
	memmove(dst2, malloc, len);
	///////
	clock_gettime(CLOCK_MONOTONIC, &end);
	////////////////////////
	printf("%ld %ld\n", start.tv_sec, start.tv_nsec);
	printf("%ld %ld\n", end.tv_sec, end.tv_nsec);

	printf("kmalloc memmove time in soft: %ld\n", end.tv_nsec - start.tv_nsec);

	rc = memcmp(malloc, dst2, len);
	rc ? printf("k memmove failed\n") : printf("k memmove successful\n");

	close(fd);

	system("rm " MMAP_KDEV);

	printf("size dst: %d\n", len);

	return 0;
}
