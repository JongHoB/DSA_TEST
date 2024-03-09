#define NPAGES 1024
#ifndef __SO2MMAP_H__
#define __SO2MMAP_H__ 1

#define PROC_VMALLOC_NAME "my-vmalloc-entry"
#define PROC_KMALLOC_NAME "my-kmalloc-entry"
#define PROC_VMALLOC_DST_NAME "my-vmalloc-dst-entry"
#define PROC_KMALLOC_DST_NAME "my-kmalloc-dst-entry"

#define MMAP_VDEV "/dev/myvmap"
#define MMAP_KDEV "/dev/mykmap"
#define MMAP_VDST_DEV "/dev/myvmap_dst"
#define MMAP_KDST_DEV "/dev/myvmap_dst"

#define virt_to_pfn(kaddr) \
    ((unsigned long)(kaddr) >> PAGE_SHIFT)
#endif
