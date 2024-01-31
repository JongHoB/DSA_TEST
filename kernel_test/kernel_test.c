// This code is modified by Jongho Baik and the original code is from Linux Kernel Lab.

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "kernel_test.h"
#include <linux/idxd.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

MODULE_DESCRIPTION("DSA in KERNEL");
MODULE_AUTHOR("Jongho Baik");
MODULE_LICENSE("Dual BSD/GPL");

#define MY_MAJOR 42

/* character device basic structure */
static struct cdev mmap_cdev;

/* pointer to the vmalloc'd area, rounded up to a page boundary */
static char *vmalloc_area;

static int my_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int my_release(struct inode *inode, struct file *filp)
{
    return 0;
}

// Read from vmalloc_area to user_buffer
// This function is called when read() is called in user space
static ssize_t my_read(struct file *file, char __user *user_buffer,
                       size_t size, loff_t *offset)
{
    /* TODO 2/2: check size doesn't exceed our mapped area size */
    if (size > NPAGES * PAGE_SIZE)
        size = NPAGES * PAGE_SIZE;

    /* TODO 2/2: copy from mapped area to user buffer */
    if (copy_to_user(user_buffer, vmalloc_area, size))
        return -EFAULT;

    return size;
}

// Write from user_buffer to vmalloc_area
// This function is called when write() is called in user space
static ssize_t my_write(struct file *file, const char __user *user_buffer,
                        size_t size, loff_t *offset)
{
    /* TODO 2/2: check size doesn't exceed our mapped area size */
    if (size > NPAGES * PAGE_SIZE)
        size = NPAGES * PAGE_SIZE;

    /* TODO 2/3: copy from user buffer to mapped area */
    memset(vmalloc_area, 0, NPAGES * PAGE_SIZE);
    if (copy_from_user(vmalloc_area, user_buffer, size))
        return -EFAULT;

    return size;
}

// Map vmalloc_area to user space
// This function is called when mmap() is called in user space
// vm_area_struct is a structure that represents a memory mapping
// vmalloc_to_pfn() returns the page frame number of a vmalloc'd area
// remap_pfn_range() maps a page frame number to a user space address
// Then when the user access the user space virtual address through mmap(),
// the kernel will translate the virtual address to the physical address
static int my_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int ret;
    long length = vma->vm_end - vma->vm_start; // length of mapping
    unsigned long start = vma->vm_start;       // start address of mapping
    char *vmalloc_area_ptr = vmalloc_area;     // pointer to vmalloc_area
    unsigned long pfn;

    if (length > NPAGES * PAGE_SIZE) // check length
        return -EIO;

    /* TODO 1/9: map pages individually */
    while (length > 0)
    {
        pfn = vmalloc_to_pfn(vmalloc_area_ptr);                               // get page frame number
        ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE, vma->vm_page_prot); // map page frame number to user space
        if (ret < 0)
            return ret;
        start += PAGE_SIZE;
        vmalloc_area_ptr += PAGE_SIZE;
        length -= PAGE_SIZE;
    }

    return 0;
}

static const struct file_operations mmap_fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .mmap = my_mmap,
    .read = my_read,
    .write = my_write};

// This function is called when cat /proc/mymap is called in user space
// seq_file is a structure that represents a file in /proc
static int my_seq_show(struct seq_file *seq, void *v)
{
    struct mm_struct *mm;
    struct vm_area_struct *vma_iterator;
    unsigned long total = 0;

// if linux version is less than 6.1, use mm->mmap
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)

    /* TODO 3: Get current process' mm_struct */
    mm = get_task_mm(current);

    /* TODO 3/6: Iterate through all memory mappings and print ranges */
    vma_iterator = mm->mmap;
    while (vma_iterator != NULL)
    {
        pr_info("%lx %lx\n", vma_iterator->vm_start, vma_iterator->vm_end);
        total += vma_iterator->vm_end - vma_iterator->vm_start;
        vma_iterator = vma_iterator->vm_next;
    }
#else
    mm = get_task_mm(current);

    VMA_ITERATOR(vmi, mm, 0);

    for_each_vma(vmi, vma_iterator)
    {
        pr_info("%lx %lx\n", vma_iterator->vm_start, vma_iterator->vm_end);
        total += vma_iterator->vm_end - vma_iterator->vm_start;
    }
#endif

    /* TODO 3: Release mm_struct */
    mmput(mm);

    /* TODO 3: write the total count to file  */
    seq_printf(seq, "%lu %s\n", total, current->comm);
    return 0;
}

// This function is called when cat /proc/mymap is called in user space
static int my_seq_open(struct inode *inode, struct file *file)
{
    /* TODO 3: Register the display function */
    return single_open(file, my_seq_show, NULL);
}

static const struct proc_ops my_proc_ops = {
    .proc_open = my_seq_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int init_dsa(void)
{
    // dmaengine subsystem
    // ls /sys/class/dma/
    // we can see dma0chan0
    // dma0chan0 is dsa that we configured
    // we need to use that dsa

    // Fisst, we need to get the dma device
    // We can get the dma device by using dma_request_channel
    // dma_request_channel is a function that returns dma_chan
    // dma_chan is a structure that represents a dma channel

    dma_cap_mask_t mask;
    dma_cap_zero(mask);
    dma_cap_set(DMA_SLAVE, mask);
    struct dma_chan *chan;
    chan = dma_request_channel(mask, NULL, NULL);

    // print the dma device name
    pr_info("%s\n", dma_chan_name(chan));
    return 0;
}
static int init_cdev(void)
{
    int ret = 0;
    int i;
    /* TODO 3/7: create a new entry in procfs */
    struct proc_dir_entry *entry;

    // This is used to create a new file in /proc
    entry = proc_create(PROC_VMALLOC_NAME, 0, NULL, &my_proc_ops);
    if (!entry)
    {
        ret = -ENOMEM;
        goto out;
    }

    // This is used to register a character device
    // The major number is MY_MAJOR
    // The minor number is 0
    // The name of the device is "mymmap"
    ret = register_chrdev_region(MKDEV(MY_MAJOR, 0), 1, "myvmap");
    if (ret < 0)
    {
        pr_err("could not register region\n");
        goto out_no_chrdev;
    }

    /* TODO 1/6: allocate NPAGES using vmalloc */
    vmalloc_area = (char *)vmalloc(NPAGES * PAGE_SIZE);
    if (vmalloc_area == NULL)
    {
        ret = -ENOMEM;
        pr_err("could not allocate memory\n");
        goto out_unreg;
    }

    /* TODO 1/2: mark pages as reserved */
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        SetPageReserved(vmalloc_to_page(vmalloc_area + i));

    // So i want to check how much distance between each page
    // Get the page frame number of each page
    // Then calculate the distance between each page
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        unsigned long pfn = vmalloc_to_pfn(vmalloc_area + i);
        pr_info("%lu\n", pfn);
    }

    /* TODO 1/6: write data in each page */
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        // Write vmalloc in each page
        sprintf(vmalloc_area + i, "vmalloc %d", i / PAGE_SIZE);

        // Last of the String is \0
        // After the null i will fill it with 1
        // So i can check if the data is written correctly
        memset(vmalloc_area + i + strlen(vmalloc_area + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area + i) - 1);
    }

    cdev_init(&mmap_cdev, &mmap_fops);
    mmap_cdev.owner = THIS_MODULE;
    ret = cdev_add(&mmap_cdev, MKDEV(MY_MAJOR, 0), 1);
    if (ret < 0)
    {
        pr_err("could not add device\n");
        goto out_vfree;
    }

    pr_info("myvmap module loaded\n");

    return 0;

out_vfree:
    vfree(vmalloc_area);
out_unreg:
    unregister_chrdev_region(MKDEV(MY_MAJOR, 0), 1);
out_no_chrdev:
    remove_proc_entry(PROC_VMALLOC_NAME, NULL);
out:
    return ret;
}

static int __init my_init(void)
{
    int init_result = init_cdev();
    if (init_result < 0)
    {
        return init_result;
    }
    int dsa_result = init_dsa();
    if (dsa_result < 0)
    {
        return dsa_result;
    }
    return 0;
}

static void exit_cdev(void)
{
    int i;

    cdev_del(&mmap_cdev);

    /* TODO 1/3: clear reservation on pages and free mem.*/
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        ClearPageReserved(vmalloc_to_page(vmalloc_area + i));
    vfree(vmalloc_area);

    unregister_chrdev_region(MKDEV(MY_MAJOR, 0), 1);
    /* TODO 3: remove proc entry */
    remove_proc_entry(PROC_VMALLOC_NAME, NULL);

    pr_info("myvmap module unloaded\n");
}

static void __exit my_exit(void)
{
    exit_cdev();
}

module_init(my_init);
module_exit(my_exit);