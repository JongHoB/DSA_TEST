#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define BLEN            4096<<4

int main()
{
	struct timeval start,end;
	double s,e;
	char src[BLEN];
	char dst[BLEN];

	memset(src, 0xaa, BLEN);
	/////////////////////////
	gettimeofday(&start,NULL);
	///////
	memmove(dst,src,BLEN);
	///////
	gettimeofday(&end,NULL);
	////////////////////////
	s=(start.tv_sec)*1000+(start.tv_usec)/1000;
	printf("%ld %ld\n",start.tv_sec,start.tv_usec);
	e=(end.tv_sec)*1000+(end.tv_usec)/1000;
	printf("%ld %ld\n",end.tv_sec,end.tv_usec);
	

	printf("memmove time in soft: %f\n",(e-s)/1000);

	int rc=memcmp(src,dst,BLEN);
	rc	?	printf("memmove failed\n")	:	printf("memmove successful\n");

	return 0;
}

