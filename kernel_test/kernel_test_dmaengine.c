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
#include <linux/export.h>

#include <linux/idxd.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/iommu.h>

#include <linux/pci.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>

#include "kernel_test.h"

MODULE_DESCRIPTION("DSA in KERNEL");
MODULE_AUTHOR("Jongho Baik");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(IDXD);

static struct dma_chan *chan = NULL;
static struct idxd_wq *wq;
struct idxd_device *idxd_device;
struct device *dev;

/* pointer to the vmalloc'd area, rounded up to a page boundary */
static char *kmalloc_area;
static char *kmalloc_area2;

static struct page *page1;
static struct page *page2;
static struct page *page3;
static struct page *page4;

static int init_dsa(void)
{

    struct pci_dev *pci_dev = NULL;
    struct pci_dev *pci_dev_list[DSA_LIST];

    struct pci_device_id pci_device_id = {PCI_DEVICE_DATA(INTEL, DSA_SPR0, NULL)};
    int device_count = 0;

    while ((pci_dev = pci_get_device(pci_device_id.vendor, pci_device_id.device, pci_dev)) != NULL)
    {
        pci_dev_list[device_count] = pci_dev;
        device_count++;
        // struct idxd_device *idxd_device = pci_get_drvdata(pci_dev);
        // pr_info("device: %d\n", device_count);
        // pr_info("wq ats support:%d\n", idxd_device->hw.wq_cap.wq_ats_support);
        // pr_info("wq prs support:%d\n", idxd_device->hw.wq_cap.wq_prs_support);
        if (device_count >= DSA_LIST)
            break;
    }

    if (device_count < DSA_LIST)
    {
        pr_err("could not find %d DSA devices\n", DSA_LIST);
        return -1;
    }

    dev = &pci_dev_list[DSA_NUM]->dev; // dsa4(3rd device)

    idxd_device = pci_get_drvdata(pci_dev_list[DSA_NUM]);

    if (!idxd_device)
    {
        pr_err("could not get idxd device\n");
        return -1;
    }
    if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd_device->flags))
    {
        pr_err("idxd device is not configurable\n");
    }
    if (idxd_device->wqs[WQ_NUM]->state != IDXD_WQ_ENABLED)
    {
        pr_err("idxd wq %d is not enabled\n", WQ_NUM);
        return -1;
    }

    wq = idxd_device->wqs[WQ_NUM]; // wq1

    if (is_idxd_wq_dmaengine(wq))
    {
        pr_info("wq is dmaengine\n");
    }
    else
    {
        pr_info("wq is not dmaengine\n");
        return 0;
    }

    if (is_idxd_wq_kernel(wq) && device_pasid_enabled(wq->idxd))
    {
        pr_info("wq is kernel and pasid enabled\n");
    }
    else
    {
        pr_info("wq is kernel and pasid not enabled\n");
    }

    if (wq->client_count > 0)
    {
        dma_release_channel(&wq->idxd_chan->chan);
    }

    chan = dma_get_slave_channel(&wq->idxd_chan->chan);

    pr_info("wq num_descs: %d\n", wq->num_descs);

    // dma infos
    pr_info("dma device name: %s\n", dma_chan_name(&wq->idxd_chan->chan));

    pr_info("wq flags: 0x%x\n", wq->flags);

    // set_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);
    // set_bit(WQ_FLAG_PRS_DISABLE, &wq->flags);
    // clear_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);
    // clear_bit(WQ_FLAG_PRS_DISABLE, &wq->flags);
    // pr_info("after wq flags: 0x%x\n", wq->flags);

    return 0;
}

// for struct page region
static int init_region_page(void)
{
    int ret = 0;
    unsigned long pfn_first, pfn_last;
    page1 = alloc_pages(GFP_KERNEL, PAGE_ORDER);

    if (!page1)
    {
        pr_err("could not allocate memory\n");
        ret = -ENOMEM;
        goto out;
    }

    pfn_first = page_to_pfn(page1);
    pfn_last = page_to_pfn(page1 + (1 << PAGE_ORDER) - 1);

    if (pfn_last - pfn_first + 1 != int_pow(2, PAGE_ORDER))
    {
        pr_info("page1 not continuous\n");
        ret = -ENOMEM;
        goto out;
    }

    page2 = alloc_pages(GFP_KERNEL, PAGE_ORDER);
    if (!page2)
    {
        pr_err("could not allocate memory\n");
        ret = -ENOMEM;
        goto out;
    }
    pfn_first = page_to_pfn(page2);
    pfn_last = page_to_pfn(page2 + (1 << PAGE_ORDER) - 1);

    if (pfn_last - pfn_first + 1 != int_pow(2, PAGE_ORDER))
    {
        pr_info("page2 not continuous\n");
        ret = -ENOMEM;
        goto out;
    }

    page3 = alloc_pages(GFP_KERNEL, PAGE_ORDER);
    if (!page3)
    {
        pr_err("could not allocate memory\n");
        ret = -ENOMEM;
        goto out;
    }
    pfn_first = page_to_pfn(page3);
    pfn_last = page_to_pfn(page3 + (1 << PAGE_ORDER) - 1);

    if (pfn_last - pfn_first + 1 != int_pow(2, PAGE_ORDER))
    {
        pr_info("page3 not continuous\n");
        ret = -ENOMEM;
        goto out;
    }

    page4 = alloc_pages(GFP_KERNEL, PAGE_ORDER);
    if (!page4)
    {
        pr_err("could not allocate memory\n");
        ret = -ENOMEM;
        goto out;
    }
    pfn_first = page_to_pfn(page4);
    pfn_last = page_to_pfn(page4 + (1 << PAGE_ORDER) - 1);

    if (pfn_last - pfn_first + 1 != int_pow(2, PAGE_ORDER))
    {
        pr_info("page4 not continuous\n");
        ret = -ENOMEM;
        goto out;
    }
    strcpy(page_address(page1), "Hello, world! page1");
    strcpy(page_address(page2), "Hello, world! page2");
    strcpy(page_address(page3), "Hello, world! page3");
    strcpy(page_address(page4), "Hello, world! page4");
    cmp = memcmp(page_address(page1), page_address(page2), PAGE_SIZE * int_pow(2, PAGE_ORDER));
    cmp ? pr_info("1 and 2 Not same pages\n") : pr_info("1 and 2 same pages\n");

    cmp = memcmp(page_address(page3), page_address(page4), PAGE_SIZE * int_pow(2, PAGE_ORDER));
    cmp ? pr_info("3 and 4 Not same pages\n") : pr_info("3 and 4 same pages\n");

    return 0;

out:
    __free_pages(page1, PAGE_ORDER);
    __free_pages(page2, PAGE_ORDER);
    __free_pages(page3, PAGE_ORDER);
    __free_pages(page4, PAGE_ORDER);

    return ret;
}

// for kmalloc region
static int init_region(void)
{
    int ret = 0;
    int i;

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
    {
        SetPageReserved(virt_to_page(kmalloc_area + i));
        SetPageReserved(virt_to_page(kmalloc_area2 + i));
    }

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        unsigned long pfn = page_to_pfn(virt_to_page(kmalloc_area + i));
        // pr_info("area 1: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area2 + i));
        // pr_info("area 2: %lu\n", pfn);
    }

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

    pr_info("mykmap module loaded\n");

    return 0;

out_kfree:
    kfree(kmalloc_area);
    kfree(kmalloc_area2);

    return ret;
}

static void dsa_copy(void)
{
    struct idxd_desc *desc = NULL;
    struct dsa_hw_desc *hw = NULL;
    struct dma_device *device = &idxd_device->idxd_dma->dma;
    dma_addr_t src1, src2, dst1, dst2;
    struct timespec64 start, end, start2, end2, start3, end3, start4, end4;
    int cmp = 0;
    int poll = 0;
    int rc = 0;
    int fault = 0;

    ///////////////////////
    // kmalloc mapping test
    ///////////////////////

    //  src1 = dma_map_single(device->dev, kmalloc_area, NPAGES * PAGE_SIZE, DMA_TO_DEVICE);

    // if (dma_mapping_error(chan->device->dev, src1))
    // {
    //     pr_info("dma_map_single error\n");
    // }

    // dst1 = dma_map_single(device->dev, kmalloc_area2, NPAGES * PAGE_SIZE, DMA_FROM_DEVICE);

    // if (dma_mapping_error(chan->device->dev, dst1))
    // {
    //     pr_info("dma_map_single error\n");
    // }

    ///////////////////////
    // phys mapping test
    // I know that dma_map_resource is for MMIO devices
    // but it works......
    ///////////////////////

    // src1 = virt_to_phys(kmalloc_area);
    // dst1 = virt_to_phys(kmalloc_area2);

    // src1 = dma_map_resource(dev, virt_to_phys(kmalloc_area), NPAGES * PAGE_SIZE, DMA_TO_DEVICE, DMA_ATTR_PRIVILEGED);

    // dst1 = dma_map_resource(dev, virt_to_phys(kmalloc_area2), NPAGES * PAGE_SIZE, DMA_FROM_DEVICE, DMA_ATTR_PRIVILEGED);

    ///////////////////////
    // struct page mapping test
    ///////////////////////

    ktime_get_ts64(&start);
    src1 = dma_map_page(dev, page1, 0, PAGE_SIZE * int_pow(2, PAGE_ORDER), DMA_TO_DEVICE);
    ktime_get_ts64(&end);

    if (dma_mapping_error(dev, src1))
    {
        pr_info("dma_map_page error\n");
    }

    ktime_get_ts64(&start2);
    dst1 = dma_map_page(dev, page2, 0, PAGE_SIZE * int_pow(2, PAGE_ORDER), DMA_FROM_DEVICE);
    ktime_get_ts64(&end2);

    if (dma_mapping_error(dev, dst1))
    {
        pr_info("dma_map_page error\n");
    }

    ///////////////////////
    // DSA_MEMCPY PROCESS
    ///////////////////////

    struct dma_async_tx_descriptor *dma_desc = device->device_prep_dma_memcpy(chan, dst1, src1, NPAGES * PAGE_SIZE, IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
    if (!dma_desc)
    {
        pr_info("device_prep_dma_memcpy error\n");
    }
    desc = container_of(dma_desc, struct idxd_desc, txd);

    hw = desc->hw;

    pr_info("opcode: 0x%x\n", hw->opcode);
    pr_info("flags: 0x%x\n", hw->flags);

    pr_info("init status: 0x%x\n", desc->completion->status);

retry:
    // pr_info("xfer_size: %d\n", desc->hw->xfer_size);

    desc->txd.cookie = desc->txd.tx_submit(&desc->txd);

    while (!desc->completion->status && poll++ < POLL_RETRY_MAX)
    {
        cpu_relax();
    }

    if (poll >= POLL_RETRY_MAX || fault >= POLL_RETRY_MAX)
    {
        pr_info("poll retry max\n");
        goto done;
    }

    if (desc->completion->status != DSA_COMP_SUCCESS)
    {
        fault++;
        if (DSA_COMP_STATUS(desc->completion->status) == DSA_COMP_PAGE_FAULT_NOBOF)
        {
            int wr = desc->completion->status & DSA_COMP_STATUS_WRITE;
            volatile char *t = (char *)desc->completion->fault_addr;
            wr ? *t = *t : *t;
            hw->src_addr += desc->completion->bytes_completed;
            hw->dst_addr += desc->completion->bytes_completed;
            hw->xfer_size -= desc->completion->bytes_completed;
            goto retry;
        }
        else
        {
            pr_info("desc failed status: 0x%x\n", desc->completion->status);
        }
    }
    else
    {
        pr_info("desc success\n");
        // cmp = memcmp(kmalloc_area, kmalloc_area2, NPAGES * PAGE_SIZE);
        // cmp ? pr_info("copy fail\n") : pr_info("copy success\n");

        cmp = memcmp(page_address(page1), page_address(page2), PAGE_SIZE * 4);
        cmp ? pr_info("Not same pages\n") : pr_info("same pages\n");
    }
done:
    pr_info("after status: 0x%x\n", desc->completion->status);

    pr_info("fault info: 0x%x\n", desc->completion->fault_info);
    pr_info("result: 0x%x\n", desc->completion->result);
    pr_info("invalid flag uint32: 0x%x\n", desc->completion->invalid_flags);

    idxd_desc_complete(desc, IDXD_COMPLETE_NORMAL, 0);
    // cmp = memcmp(kmalloc_area, kmalloc_area2, NPAGES * PAGE_SIZE);
    // cmp ? pr_info("copy fail\n") : pr_info("copy success\n");

    cmp = memcmp(page_address(page1), page_address(page2), PAGE_SIZE * 4);
    cmp ? pr_info("Not same pages\n") : pr_info("same pages\n");
    pr_info("page1: %s\n", page_address(page1));
    pr_info("page2: %s\n", page_address(page2));

    ///////////////////////
    // kmalloc mapping test
    ///////////////////////

    // dma_unmap_single(device->dev, src1, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);
    // dma_unmap_single(device->dev, dst1, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);

    ///////////////////////
    // phys mapping test
    // I know that dma_map_resource is for MMIO devices
    // but it works......
    ///////////////////////

    // dma_unmap_resource(dev, src1, NPAGES * PAGE_SIZE, DMA_TO_DEVICE, DMA_ATTR_PRIVILEGED);
    // dma_unmap_resource(dev, dst1, NPAGES * PAGE_SIZE, DMA_FROM_DEVICE, DMA_ATTR_PRIVILEGED);

    ///////////////////////
    // struct page mapping test
    ///////////////////////

    dma_unmap_page(dev, src1, PAGE_SIZE * int_pow(2, PAGE_ORDER), DMA_TO_DEVICE);
    dma_unmap_page(dev, dst1, PAGE_SIZE * int_pow(2, PAGE_ORDER), DMA_FROM_DEVICE);

    // memmove(kmalloc_area4, kmalloc_area3, NPAGES * PAGE_SIZE);

    // cmp = memcmp(kmalloc_area3, kmalloc_area4, NPAGES * PAGE_SIZE);
    // cmp ? pr_info("copy fail\n") : pr_info("copy success\n");

    // pr_info("time1: %lld\n", end.tv_nsec - start.tv_nsec);
    // pr_info("time2: %lld\n", end2.tv_nsec - start2.tv_nsec);
    // pr_info("time3: %lld\n", end3.tv_nsec - start3.tv_nsec);
    // pr_info("time4: %lld\n", end4.tv_nsec - start4.tv_nsec);

    return;
}

static int __init
my_init(void)
{
    // int init_result = init_region();
    int init_result = init_region_page();
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

static void exit_module(void)
{
    int i;

    /* TODO 1/3: clear reservation on pages and free mem.*/
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        ClearPageReserved(virt_to_page(kmalloc_area + i));
    kfree(kmalloc_area);

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
        ClearPageReserved(virt_to_page(kmalloc_area2 + i));
    kfree(kmalloc_area2);

    pr_info("myvmap module unloaded\n");
}

static void exit_module_page(void)
{
    __free_pages(page1, PAGE_ORDER);
    __free_pages(page2, PAGE_ORDER);
    __free_pages(page3, PAGE_ORDER);
    __free_pages(page4, PAGE_ORDER);
    pr_info("myvmap module unloaded\n");
}

static void __exit my_exit(void)
{
    // exit_module();
    exit_module_page();
    dma_release_channel(chan);
}

module_init(my_init);
module_exit(my_exit);
