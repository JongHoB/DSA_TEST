#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include "memmove.h"

#include <time.h>

int main()
{
	struct timespec start, end;
	double s, e;
	int BLEN = NPAGES * getpagesize();
	char src[BLEN];
	char dst[BLEN];

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
	/////////////////////////
	clock_gettime(CLOCK_MONOTONIC, &start);
	///////
	memmove(dst, src, BLEN);
	///////
	clock_gettime(CLOCK_MONOTONIC, &end);
	////////////////////////

	printf("memmove time in soft: %ld\n", end.tv_nsec - start.tv_nsec);
	printf("size dst: %d\n", sizeof(dst) / sizeof(char));

	int rc = memcmp(src, dst, BLEN);
	rc ? printf("memmove failed\n") : printf("memmove successful\n");

	return 0;
}
