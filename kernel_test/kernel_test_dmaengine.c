// This code is modified by Jongho Baik and the original code is from Linux Kernel Lab.
#include "/usr/src/linux-6.8-rc2/drivers/dma/idxd/idxd.h"
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

#include <linux/idxd.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/iommu.h>

#include <linux/pci.h>

#include "kernel_test.h"

MODULE_DESCRIPTION("DSA in KERNEL");
MODULE_AUTHOR("Jongho Baik");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(IDXD);

static struct dma_chan *chan = NULL;
static struct idxd_wq *wq;
struct idxd_device *idxd_device;
// static struct idxd_device *idxd_device;
// static struct idxd_dma_chan *idxd_chan;

#define MY_MAJOR 42

/* character device basic structure */
// static struct cdev mmap_cdev;

/* pointer to the vmalloc'd area, rounded up to a page boundary */
static char *kmalloc_area;
static char *kmalloc_area2;

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
    if (copy_to_user(user_buffer, kmalloc_area, size))
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
    memset(kmalloc_area, 0, NPAGES * PAGE_SIZE);
    if (copy_from_user(kmalloc_area, user_buffer, size))
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
    char *kmalloc_area_ptr = kmalloc_area;     // pointer to kmalloc_area
    unsigned long pfn;

    if (length > NPAGES * PAGE_SIZE) // check length
        return -EIO;

    pfn = page_to_pfn(virt_to_page(kmalloc_area_ptr));                 // get page frame number
    ret = remap_pfn_range(vma, start, pfn, length, vma->vm_page_prot); // map page frame number to user space
    if (ret < 0)
        return ret;

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

// static int load_dsa_device2(struct idxd_device *idxd)
// {
//     struct idxd_engine *engine;
//     struct idxd_group *group;

//     if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
//         return 0;

//     wq = idxd->wqs[1];

//     if (wq->state != IDXD_WQ_DISABLED)
//         return -EPERM;

//     /* set mode to "dedicated" */
//     set_bit(WQ_FLAG_DEDICATED, &wq->flags);
//     wq->threshold = 0;

//     /* only setting up 1 wq, so give it all the wq space */
//     wq->size = idxd->max_wq_size;

//     /* set priority to 10 */
//     wq->priority = 10;

//     /* set type to "kernel" */
//     wq->type = IDXD_WQT_KERNEL;

//     /* set wq group to 0 */
//     group = idxd->groups[1];
//     wq->group = group;
//     group->num_wqs++;

//     /* set name to "iaa_crypto" */
//     memset(wq->name, 0, WQ_NAME_SIZE + 1);
//     strscpy(wq->name, "dmaengine", WQ_NAME_SIZE + 1);

//     /* set driver_name to "crypto" */
//     memset(wq->driver_name, 0, DRIVER_NAME_SIZE + 1);
//     strscpy(wq->driver_name, "dmaengine", DRIVER_NAME_SIZE + 1);

//     engine = idxd->engines[1];

//     /* set engine group to 0 */
//     engine->group = idxd->groups[1];
//     engine->group->num_engines++;

//     return 0;
// }

static int init_dsa(void)
{

    struct pci_dev *pci_dev = NULL;
    struct pci_dev *pci_dev_list[8];
    struct device *dev = NULL;
    struct pci_device_id pci_device_id = {PCI_DEVICE_DATA(INTEL, DSA_SPR0, NULL)};
    int device_count = 0;
    unsigned int pasid;
    struct iommu_sva *sva;

    while ((pci_dev = pci_get_device(pci_device_id.vendor, pci_device_id.device, pci_dev)) != NULL)
    {
        pci_dev_list[device_count] = pci_dev;
        device_count++;
        struct device *dev = &pci_dev->dev;
        pr_info("device name: %s\n", dev_name(dev));
        if (device_count >= 8)
            break;
    }

    if (device_count < 8)
    {
        pr_err("could not find 8 DSA devices\n");
        return -1;
    }
    pr_info("device count: %d\n", device_count);

    dev = &pci_dev_list[1]->dev;

    idxd_device = pci_get_drvdata(pci_dev_list[1]);

    if (!idxd_device)
    {
        pr_err("could not get idxd device\n");
        return -1;
    }
    if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd_device->flags))
    {
        pr_err("idxd device is not configurable\n");
    }

    pr_info("idxd->hw.version: %d\n", idxd_device->hw.version);
    pr_info("idxd pasid: %d\n", idxd_device->pasid);

    pr_info("idxd wq 1 state: %d\n", idxd_device->wqs[1]->state);

    wq = idxd_device->wqs[1];
    if (wq_pasid_enabled(wq))
    {
        pr_info("wq pasid enabled\n");
    }
    else
    {
        pr_info("wq pasid not enabled\n");
    }

    if (is_idxd_wq_dmaengine(wq))
    {
        pr_info("wq is dmaengine\n");
    }
    else
    {
        pr_info("wq is not dmaengine\n");
        return 0;
    }
    // idxd_wq_get(wq);
    chan = dma_get_slave_channel(&wq->idxd_chan->chan);
    pr_info("client count: %d\n", wq->client_count);

    pr_info("wq num_descs: %d\n", wq->num_descs);

    // dma infos
    pr_info("dma info\n");
    pr_info("dma device name: %s\n", dma_chan_name(&wq->idxd_chan->chan));

    // wq->flags = 0;
    pr_info("wq flags: 0x%x\n", wq->flags);
    // set_bit(WQ_FLAG_DEDICATED, &wq->flags);

    // pr_info("before wq flags: 0x%x\n", wq->flags);
    clear_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);
    clear_bit(WQ_FLAG_PRS_DISABLE, &wq->flags);
    // clear_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags);
    pr_info("after wq flags: 0x%x\n", wq->flags);

    // sva = iommu_sva_bind_device(dev, current->mm);
    // if (IS_ERR(sva))
    // {
    //     pr_info("iommu_sva_bind_device failed\n");
    // }
    // else
    // {
    //     pr_info("iommu_sva_bind_device success\n");
    // }
    // pasid = iommu_sva_get_pasid(sva);

    // pr_info("pasid: %d\n", pasid);
    // if (idxd_wq_set_pasid(wq, pasid) < 0)
    // {
    //     pr_info("idxd_wq_set_pasid failed\n");
    // }
    // else
    // {
    //     pr_info("idxd_wq_set_pasid success\n");
    // }

    return 0;
}
static int init_cdev(void)
{
    int ret = 0;
    int i;
    /* TODO 3/7: create a new entry in procfs */
    // struct proc_dir_entry *entry;

    // // This is used to create a new file in /proc
    // entry = proc_create(PROC_KMALLOC_NAME, 0, NULL, &my_proc_ops);
    // if (!entry)
    // {
    //     ret = -ENOMEM;
    //     goto out;
    // }

    // // This is used to register a character device
    // // The major number is MY_MAJOR
    // // The minor number is 0
    // // The name of the device is "mymmap"
    // ret = register_chrdev_region(MKDEV(MY_MAJOR, 2), 1, "mykmap");
    // if (ret < 0)
    // {
    //     pr_err("could not register region\n");
    //     goto out_no_chrdev;
    // }

    /* TODO 1/6: allocate NPAGES using kmalloc */
    kmalloc_area = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    kmalloc_area2 = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    if (kmalloc_area == NULL)
    {
        ret = -ENOMEM;
        pr_err("could not allocate memory\n");
        goto out_kfree;
    }
    if (kmalloc_area2 == NULL)
    {
        ret = -ENOMEM;
        pr_err("could not allocate memory\n");
        goto out_kfree;
    }

    /* TODO 1/2: mark pages as reserved */
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        SetPageReserved(virt_to_page(kmalloc_area + i));

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        SetPageReserved(virt_to_page(kmalloc_area2 + i));

    // So i want to check how much distance between each page
    // Get the page frame number of each page
    // Then calculate the distance between each page
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        unsigned long pfn = page_to_pfn(virt_to_page(kmalloc_area + i));
        pr_info("area 1: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area2 + i));
        pr_info("area 2: %lu\n", pfn);
    }

    /* TODO 1/6: write data in each page */
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        // Write kmalloc in each page
        sprintf(kmalloc_area + i, "kmalloc %ld", i / PAGE_SIZE);
        sprintf(kmalloc_area2 + i, "kmalloc2 %ld", i / PAGE_SIZE);

        // Last of the String is \0
        // After the null i will fill it with 1
        // So i can check if the data is written correctly
        memset(kmalloc_area + i + strlen(kmalloc_area + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area + i) - 1);
        memset(kmalloc_area2 + i + strlen(kmalloc_area2 + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area2 + i) - 1);
    }

    // cdev_init(&mmap_cdev, &mmap_fops);
    // mmap_cdev.owner = THIS_MODULE;
    // ret = cdev_add(&mmap_cdev, MKDEV(MY_MAJOR, 0), 1);
    // if (ret < 0)
    // {
    //     pr_err("could not add device\n");
    //     goto out_kfree;
    // }

    pr_info("mykmap module loaded\n");

    return 0;

out_kfree:
    kfree(kmalloc_area);
    // out_unreg:
    //     unregister_chrdev_region(MKDEV(MY_MAJOR, 2), 1);
    // out_no_chrdev:
    //     remove_proc_entry(PROC_KMALLOC_NAME, NULL);
    // out:
    return ret;
}

static void dsa_copy(void)
{
    // struct idxd_desc *desc = wq->descs[0];
    struct idxd_desc *desc = NULL;
    struct dsa_hw_desc *hw = NULL;
    struct dma_device *device = &idxd_device->idxd_dma->dma;
    int cmp = 0;
    int poll = 0;
    int rc = 0;

    if (IS_ERR(desc))
    {
        pr_info("error\n");
    }

    dma_addr_t src = dma_map_single(device->dev, kmalloc_area, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);
    if (dma_mapping_error(chan->device->dev, src))
    {
        pr_info("dma_map_single error\n");
    }
    dma_addr_t dst = dma_map_single(device->dev, kmalloc_area2, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);
    if (dma_mapping_error(chan->device->dev, dst))
    {
        pr_info("dma_map_single error\n");
    }

    struct dma_async_tx_descriptor *dma_desc = device->device_prep_dma_memcpy(chan, dst, src, NPAGES * PAGE_SIZE, IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC);
    if (!dma_desc)
    {
        pr_info("device_prep_dma_memcpy error\n");
    }
    desc = container_of(dma_desc, struct idxd_desc, txd);

    hw = desc->hw;

    pr_info("opcode: 0x%x\n", hw->opcode);
    pr_info("flags: 0x%x\n", hw->flags);
    pr_info("completion_addr: 0x%llx\n", hw->completion_addr);

    pr_info("desc address: 0x%llx\n", desc->compl_dma);
    pr_info("hw priv: %d\n", hw->priv);

    pr_info("init status: 0x%x\n", desc->completion->status);

    pr_info("xfer_size: %d\n", desc->hw->xfer_size);

    desc->txd.cookie = desc->txd.tx_submit(&desc->txd);
    pr_info("cookie: %d\n", desc->txd.cookie);

    while (!desc->completion->status && poll++ < POLL_RETRY_MAX)
    {
        cpu_relax();
    }

    pr_info("after status: 0x%x\n", desc->completion->status);

    cmp = memcmp(kmalloc_area, kmalloc_area2, NPAGES * PAGE_SIZE);
    if (cmp == 0)
    {
        pr_info("copy success\n");
        pr_info("kmalloc_area: %s\n", kmalloc_area);
        pr_info("kmalloc_area2: %s\n", kmalloc_area2);
    }
    else
    {
        pr_info("copy fail\n");
        pr_info("kmalloc_area: %s\n", kmalloc_area);
        pr_info("kmalloc_area2: %s\n", kmalloc_area2);
    }
    pr_info("fault info: 0x%x\n", desc->completion->fault_info);
    pr_info("result: 0x%x\n", desc->completion->result);
    pr_info("invalid flag uint32: 0x%x\n", desc->completion->invalid_flags);

    idxd_desc_complete(desc, IDXD_COMPLETE_NORMAL, 0);

    dma_unmap_single(device->dev, src, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);
    dma_unmap_single(device->dev, dst, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);

    return;
}

static int __init
my_init(void)
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
    dsa_copy();
    return 0;
}

static void exit_cdev(void)
{
    int i;

    // cdev_del(&mmap_cdev);

    /* TODO 1/3: clear reservation on pages and free mem.*/
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        ClearPageReserved(virt_to_page(kmalloc_area + i));
    kfree(kmalloc_area);

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        ClearPageReserved(virt_to_page(kmalloc_area2 + i));
    kfree(kmalloc_area2);

    // unregister_chrdev_region(MKDEV(MY_MAJOR, 2), 1);
    // /* TODO 3: remove proc entry */
    // remove_proc_entry(PROC_KMALLOC_NAME, NULL);

    pr_info("myvmap module unloaded\n");
}

static void __exit my_exit(void)
{
    exit_cdev();
    dma_release_channel(chan);
    // idxd_wq_put(wq);
}

module_init(my_init);
module_exit(my_exit);