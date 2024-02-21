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
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
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
static char *vmalloc_area;
static char *vmalloc_area2;
static char *vmalloc_area3;
static char *vmalloc_area4;

struct sg_table *sgt1;
struct sg_table *sgt2;
struct sg_table *sgt3;
struct sg_table *sgt4;

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

    return 0;
}

// for vmalloc region
static int init_region(void)
{
    int ret = 0;
    int i;

    /* TODO 1/6: allocate NPAGES using kmalloc */
    vmalloc_area = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area2 = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area3 = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area4 = (char *)vmalloc(NPAGES * PAGE_SIZE);

    if (vmalloc_area == NULL || vmalloc_area2 == NULL || vmalloc_area3 == NULL || vmalloc_area4 == NULL)
    {
        ret = -ENOMEM;
        pr_err("could not allocate memory\n");
        goto out_vfree;
    }

    /* TODO 1/2: mark pages as reserved */
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        SetPageReserved(vmalloc_to_page(vmalloc_area + i));
        SetPageReserved(vmalloc_to_page(vmalloc_area2 + i));
        SetPageReserved(vmalloc_to_page(vmalloc_area3 + i));
        SetPageReserved(vmalloc_to_page(vmalloc_area4 + i));
    }

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        unsigned long pfn = vmalloc_to_pfn(vmalloc_area + i);
        // pr_info("area 1: %lu\n", pfn);
        pfn = vmalloc_to_pfn(vmalloc_area2 + i);
        // pr_info("area 2: %lu\n", pfn);
        pfn = vmalloc_to_pfn(vmalloc_area3 + i);
        // pr_info("area 3: %lu\n", pfn);
        pfn = vmalloc_to_pfn(vmalloc_area4 + i);
        // pr_info("area 4: %lu\n", pfn);
    }

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        // Write kmalloc in each page
        sprintf(vmalloc_area + i, "vmalloc %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area2 + i, "vmalloc2 %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area3 + i, "vmalloc3 %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area4 + i, "vmalloc4 %ld", i / PAGE_SIZE);

        // Last of the String is \0
        // After the null i will fill it with 1
        // So i can check if the data is written correctly
        memset(vmalloc_area + i + strlen(vmalloc_area + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area + i) - 1);
        memset(vmalloc_area2 + i + strlen(vmalloc_area2 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area2 + i) - 1);
        memset(vmalloc_area3 + i + strlen(vmalloc_area3 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area3 + i) - 1);
        memset(vmalloc_area4 + i + strlen(vmalloc_area4 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area4 + i) - 1);
    }

    return 0;

out_vfree:
    vfree(vmalloc_area);
    vfree(vmalloc_area2);
    vfree(vmalloc_area3);
    vfree(vmalloc_area4);

    return ret;
}

static void vmalloc_to_sgtable(void)
{
    // make vmalloc area to sgtable
    int i;
    struct scatterlist *sg;
    struct page *page;
    sgt1 = kzalloc(sizeof(*sgt1), GFP_KERNEL);
    sgt2 = kzalloc(sizeof(*sgt2), GFP_KERNEL);

    if (sg_alloc_table(sgt1, NPAGES, GFP_KERNEL) || sg_alloc_table(sgt2, NPAGES, GFP_KERNEL) || sg_alloc_table(sgt3, NPAGES, GFP_KERNEL) || sg_alloc_table(sgt4, NPAGES, GFP_KERNEL))
    {
        pr_info("sg_alloc_table failed\n");
        goto free_sgt;
    }

    for_each_sgtable_sg(sgt1, sg, i)
    {
        page = vmalloc_to_page(vmalloc_area + i * PAGE_SIZE);
        if (!page)
        {
            goto free_sgt;
        }
        sg_set_page(sg, page, PAGE_SIZE, 0);
    }
    pr_info("sgt1 nents: %d\n sgt1 orig_nents: %d\n", sgt1->nents, sgt1->orig_nents);
    for_each_sgtable_sg(sgt2, sg, i)
    {
        page = vmalloc_to_page(vmalloc_area2 + i * PAGE_SIZE);
        if (!page)
        {
            goto free_sgt;
        }
        sg_set_page(sg, page, PAGE_SIZE, 0);
    }
    pr_info("sgt2 nents: %d\n sgt2 orig_nents: %d\n", sgt2->nents, sgt2->orig_nents);

    return;

free_sgt:
    sg_free_table(sgt1);
    sg_free_table(sgt2);

    return;
}

static void sgtable_to_dma_map(void)
{
    int ret;

    ret = dma_map_sgtable(dev, sgt1, DMA_TO_DEVICE, 0);
    if (ret)
    {
        pr_info("dma_map_sgtable failed\n");
        return;
    }
    ret = dma_map_sgtable(dev, sgt2, DMA_FROM_DEVICE, 0);
    if (ret)
    {
        pr_info("dma_map_sgtable failed\n");
        return;
    }
    return;
}

static void dsa_copy(void)
{
    struct idxd_desc *idxd_desc = NULL;
    struct dsa_hw_desc *hw = NULL;
    struct dma_device *device = &idxd_device->idxd_dma->dma;
    dma_addr_t src1, src2, dst1, dst2;
    struct timespec64 start, end, start2, end2, start3, end3, start4, end4;
    int cmp = 0;
    int poll = 0;
    int ret = 0;
    int fault = 0;
    int nents = 0;

    ///////////////////////
    // DSA_MEMCPY PROCESS
    ///////////////////////

    if (sgt1->nents > 1 && sgt2->nents > 1)
    {
        idxd_desc = idxd_desc_dma_submit_memcpy_sg(chan, sgt2, sgt1, min_t(unsigned int, sgt1->nents, sgt2->nents), IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
        pr_info("sg1 nents: %d\n", sgt1->nents);
        pr_info("sg2 nents: %d\n", sgt2->nents);
    }
    else
    {
        idxd_desc = idxd_desc_dma_submit_memcpy(chan, sg_dma_address(sgt2->sgl), sg_dma_address(sgt1->sgl), min_t(unsigned int, sg_dma_len(sgt1->sgl), sg_dma_len(sgt2->sgl)), IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
        pr_info("xfer_size: %d\n", idxd_desc->hw->xfer_size);
    }

    if (IS_ERR(idxd_desc))
    {
        dev_dbg(dev, "Failed to allocate descriptor\n");
        dev_dbg(dev, "Error code: %ld\n", PTR_ERR(idxd_desc));
        return;
    }

    hw = idxd_desc->hw;

    pr_info("opcode: 0x%x\nflags: 0x%x\ninit status: 0x%x\n", hw->opcode, hw->flags, idxd_desc->completion->status);

retry:
    // pr_info("xfer_size: %d\n", idxd_desc->hw->xfer_size);

    // ret = idxd_submit_desc(wq, idxd_desc);
    // if (ret)
    // {
    //     pr_info("submit failed\n");
    //     goto out;
    // }
    ktime_get_ts64(&start3);
    idxd_submit_desc(wq, idxd_desc);

    while (!idxd_desc->completion->status && poll++ < POLL_RETRY_MAX)
    {
        cpu_relax();
    }
    ktime_get_ts64(&end3);
    if (poll >= POLL_RETRY_MAX || fault >= FAULT_RETRY_MAX)
    {
        pr_info("poll retry max\n");
        goto done;
    }

    if (idxd_desc->completion->status != DSA_COMP_SUCCESS)
    {
        fault++;
    }
    else
    {
        pr_info("desc success\n");
        pr_info("poll: %d\nfault: %d\n", poll, fault);
    }
done:
    pr_info("after status: 0x%x\nfault info: 0x%x\n", idxd_desc->completion->status, idxd_desc->completion->fault_info);
    pr_info("result: 0x%x\ninvalid flag uint32: 0x%x\n", idxd_desc->completion->result, idxd_desc->completion->invalid_flags);

    pr_info("poll: %d\nfault: %d\n", poll, fault);

    // idxd_desc_complete(idxd_desc, IDXD_COMPLETE_NORMAL, 0);

    // cmp = memcmp(vmalloc_area, vmalloc_area2, NPAGES * PAGE_SIZE);
    // cmp ? pr_info("copy fail\n") : pr_info("copy success\n");

    cmp = memcmp(vmalloc_area, vmalloc_area2, NPAGES * PAGE_SIZE);
    cmp ? pr_info("DSA FAIL\n") : pr_info("DSA Success\n");
    // pr_info("page1: %s\n", page_address(page1));
    // pr_info("page2: %s\n", page_address(page2));

    // memmove with CPU
    // pr_info("memmove start\n");

    // ktime_get_ts64(&start4);
    // memcpy(page_address(page4), page_address(page3), PAGE_SIZE * int_pow(2, PAGE_ORDER));
    // ktime_get_ts64(&end4);

    // cmp = memcmp(page_address(page4), page_address(page3), PAGE_SIZE * int_pow(2, PAGE_ORDER));
    // cmp ? pr_info("memmove copy fail\n") : pr_info("memmove copy success\n");

    // pr_info("src1 map time1: %lld\n", timespec64_to_ns(&end) - timespec64_to_ns(&start));
    // pr_info("dest1 map time2: %lld\n", timespec64_to_ns(&end2) - timespec64_to_ns(&start2));
    // pr_info("DSA memmove time3: %lld\n", timespec64_to_ns(&end3) - timespec64_to_ns(&start3));
    // pr_info("memmove time4: %lld\n", timespec64_to_ns(&end4) - timespec64_to_ns(&start4));

out:
    ///////////////////////
    // kmalloc mapping test
    ///////////////////////

    // dma_unmap_single(device->dev, src1, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);
    // dma_unmap_single(device->dev, dst1, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);

    return;
}

static int __init
my_init(void)
{
    pr_info("mykmap module loaded\n");
    int init_result = init_region();
    if (init_result < 0)
    {
        return init_result;
    }
    pr_info("init_region success\n");
    vmalloc_to_sgtable();
    pr_info("vmalloc_to_sgtable success\n");
    int dsa_result = init_dsa();
    if (dsa_result < 0)
    {
        return dsa_result;
    }
    pr_info("init_dsa success\n");
    sgtable_to_dma_map();
    pr_info("sgtable_to_dma_map success\n");
    dsa_copy();
    return 0;
}

static void exit_module(void)
{
    int i;

    /* TODO 1/3: clear reservation on pages and free mem.*/
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        ClearPageReserved(vmalloc_to_page(vmalloc_area + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area2 + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area3 + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area4 + i));
    }

    vfree(vmalloc_area);
    vfree(vmalloc_area2);
    vfree(vmalloc_area3);
    vfree(vmalloc_area4);
}

static void __exit my_exit(void)
{
    exit_module();
    dma_release_channel(chan);
    pr_info("myvmap module unloaded\n");
}

module_init(my_init);
module_exit(my_exit);
