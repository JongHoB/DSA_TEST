#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define BLEN 4096 << 8

int main()
{
	struct timeval start, end;
	double s, e;
	char src[BLEN];
	char dst[BLEN];

	memset(src, 0xaa, BLEN);
	/////////////////////////
	gettimeofday(&start, NULL);
	///////
	memmove(dst, src, BLEN);
	///////
	gettimeofday(&end, NULL);
	////////////////////////
	s = (start.tv_sec) * 1000 + (start.tv_usec) / 1000;
	e = (end.tv_sec) * 1000 + (end.tv_usec) / 1000;

	printf("memmove time in soft: %ld\n", end.tv_usec - start.tv_usec);
	printf("size dst: %d\n", sizeof(dst) / sizeof(char));

	int rc = memcmp(src, dst, BLEN);
	rc ? printf("memmove failed\n") : printf("memmove successful\n");

	return 0;
}
