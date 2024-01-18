# Build Kernel Module vmalloc_to_userspace.c and user program memmove_vmalloc.c

obj-m += vmalloc_to_userspace.o kmalloc_to_userspace.o vmalloc_to_userspace_dst.o
DEBUG_CFLAGS += -g
ccflags-y += ${DBUG_CFLAGS}
CC += ${DEBUG_CFLAGS}

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 
	$(CC) -o mmap_to_userspace_soft_v mmap_to_userspace_soft_v.c
	$(CC) -o mmap_to_userspace_soft_k mmap_to_userspace_soft_k.c
	$(CC) -o mmap_to_userspace_dsa_v mmap_to_userspace_dsa_v.c	-laccel-config
	$(CC) -o mmap_to_userspace_dsa_k mmap_to_userspace_dsa_k.c	-laccel-config

debug:
	$(MAKE) -C $(KDIR) M=$(PWD) modules EXTRA_CFLAGS="$(DEBUG_CFLAGS)"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf mmap_to_userspace_soft_v mmap_to_userspace_dsa_v mmap_to_userspace_soft_k mmap_to_userspace_dsa_k