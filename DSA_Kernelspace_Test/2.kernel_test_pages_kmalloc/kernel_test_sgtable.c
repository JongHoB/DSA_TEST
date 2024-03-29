
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

LIST_HEAD(idxd_desc_lists);

static struct dma_chan *chan = NULL;
static struct idxd_wq *wq;
struct idxd_device *idxd_device;
struct device *dev;

/* pointer to the vmalloc'd area, rounded up to a page boundary */
static char *kmalloc_area;
static char *kmalloc_area2;
static char *kmalloc_area3;
static char *kmalloc_area4;
static char *kmalloc_area5;
static char *kmalloc_area6;

struct sg_table *sgt1;
struct sg_table *sgt2;
struct sg_table *sgt3;
struct sg_table *sgt4;

struct timespec64 start, end, start2, start3, end3, start4, end4, start5, end5, start6, end6, start7, end7, start8, end8, start9, end9, start10, end10, start11, end11, start12, end12, start13, end13;

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

    pr_info("wq flags: 0x%lx\n", wq->flags);

    return 0;
}

// for vmalloc region
static int init_region(void)
{
    int ret = 0;
    int i;

    pr_info("transfer size: %ld\n", NPAGES * PAGE_SIZE);

    /* TODO 1/6: allocate NPAGES using kmalloc */
    kmalloc_area = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    kmalloc_area2 = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    kmalloc_area3 = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    kmalloc_area4 = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    kmalloc_area5 = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);
    kmalloc_area6 = (char *)kmalloc(NPAGES * PAGE_SIZE, GFP_KERNEL);

    if (kmalloc_area == NULL || kmalloc_area2 == NULL || kmalloc_area3 == NULL || kmalloc_area4 == NULL || kmalloc_area5 == NULL || kmalloc_area6 == NULL)
    {
        ret = -ENOMEM;
        pr_err("could not allocate memory\n");
        goto out_vfree;
    }

    /* TODO 1/2: mark pages as reserved */
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        SetPageReserved(virt_to_page(kmalloc_area + i));
        SetPageReserved(virt_to_page(kmalloc_area2 + i));
        SetPageReserved(virt_to_page(kmalloc_area3 + i));
        SetPageReserved(virt_to_page(kmalloc_area4 + i));
        SetPageReserved(virt_to_page(kmalloc_area5 + i));
        SetPageReserved(virt_to_page(kmalloc_area6 + i));
    }

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        unsigned long pfn = page_to_pfn(virt_to_page(kmalloc_area + i));
        // pr_info("area 1: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area2 + i));
        // pr_info("area 2: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area3 + i));
        // pr_info("area 3: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area4 + i));
        // pr_info("area 4: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area5 + i));
        // pr_info("area 5: %lu\n", pfn);
        pfn = page_to_pfn(virt_to_page(kmalloc_area6 + i));
        // pr_info("area 6: %lu\n", pfn);
    }

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        // Write kmalloc in each page
        sprintf(kmalloc_area + i, "kmalloc %ld", i / PAGE_SIZE);
        sprintf(kmalloc_area2 + i, "kmalloc2 %ld", i / PAGE_SIZE);
        sprintf(kmalloc_area3 + i, "kmalloc3 %ld", i / PAGE_SIZE);
        sprintf(kmalloc_area4 + i, "kmalloc4 %ld", i / PAGE_SIZE);
        sprintf(kmalloc_area5 + i, "kmalloc5 %ld", i / PAGE_SIZE);
        sprintf(kmalloc_area6 + i, "kmalloc6 %ld", i / PAGE_SIZE);

        // Last of the String is \0
        // After the null i will fill it with 1
        // So i can check if the data is written correctly
        memset(kmalloc_area + i + strlen(kmalloc_area + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area + i) - 1);
        memset(kmalloc_area2 + i + strlen(kmalloc_area2 + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area2 + i) - 1);
        memset(kmalloc_area3 + i + strlen(kmalloc_area3 + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area3 + i) - 1);
        memset(kmalloc_area4 + i + strlen(kmalloc_area4 + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area4 + i) - 1);
        memset(kmalloc_area5 + i + strlen(kmalloc_area5 + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area5 + i) - 1);
        memset(kmalloc_area6 + i + strlen(kmalloc_area6 + i) + 1, '0', PAGE_SIZE - strlen(kmalloc_area6 + i) - 1);
    }

    return 0;

out_vfree:
    kfree(kmalloc_area);
    kfree(kmalloc_area2);
    kfree(kmalloc_area3);
    kfree(kmalloc_area4);
    kfree(kmalloc_area5);
    kfree(kmalloc_area6);

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

    if (sg_alloc_table(sgt1, NPAGES, GFP_KERNEL) || sg_alloc_table(sgt2, NPAGES, GFP_KERNEL))
    {
        pr_info("sg_alloc_table failed\n");
        goto free_sgt;
    }

    for_each_sgtable_sg(sgt1, sg, i)
    {
        page = virt_to_page(kmalloc_area + i * PAGE_SIZE);
        if (!page)
        {
            goto free_sgt;
        }
        sg_set_page(sg, page, PAGE_SIZE, 0);
    }
    // pr_info("sgt1 nents: %d\n sgt1 orig_nents: %d\n", sgt1->nents, sgt1->orig_nents);
    for_each_sgtable_sg(sgt2, sg, i)
    {
        page = virt_to_page(kmalloc_area2 + i * PAGE_SIZE);
        if (!page)
        {
            goto free_sgt;
        }
        sg_set_page(sg, page, PAGE_SIZE, 0);
    }
    // pr_info("sgt2 nents: %d\n sgt2 orig_nents: %d\n", sgt2->nents, sgt2->orig_nents);

    return;

free_sgt:
    sg_free_table(sgt1);
    sg_free_table(sgt2);

    return;
}

static void sgtable_to_dma_map(void)
{
    int ret;

    ktime_get_ts64(&start);
    ret = dma_map_sgtable(dev, sgt1, DMA_TO_DEVICE, 0);
    ktime_get_ts64(&end);
    if (ret)
    {
        pr_info("dma_map_sgtable failed\n");
        return;
    }
    ktime_get_ts64(&start5);
    ret = dma_map_sgtable(dev, sgt2, DMA_FROM_DEVICE, 0);
    ktime_get_ts64(&end5);
    if (ret)
    {
        pr_info("dma_map_sgtable failed\n");
        return;
    }
    // pr_info("sgt2 nents: %d\n sgt2 orig_nents: %d\n", sgt2->nents, sgt2->orig_nents);
    return;
}

static void test(void)
{
    // make vmalloc area to sgtable
    int i;
    int ret;
    struct scatterlist *sg;
    struct page *page;
    sgt3 = kzalloc(sizeof(*sgt3), GFP_KERNEL);
    sgt4 = kzalloc(sizeof(*sgt4), GFP_KERNEL);

    if (sg_alloc_table(sgt3, NPAGES, GFP_KERNEL) || sg_alloc_table(sgt4, NPAGES, GFP_KERNEL))
    {
        pr_info("sg_alloc_table failed\n");
        goto free_sgt;
    }

    for_each_sgtable_sg(sgt3, sg, i)
    {
        page = virt_to_page(kmalloc_area5 + i * PAGE_SIZE);
        if (!page)
        {
            goto free_sgt;
        }
        sg_set_page(sg, page, PAGE_SIZE, 0);
    }
    // pr_info("sgt1 nents: %d\n sgt1 orig_nents: %d\n", sgt1->nents, sgt1->orig_nents);
    for_each_sgtable_sg(sgt4, sg, i)
    {
        page = virt_to_page(kmalloc_area6 + i * PAGE_SIZE);
        if (!page)
        {
            goto free_sgt;
        }
        sg_set_page(sg, page, PAGE_SIZE, 0);
    }
    // pr_info("sgt2 nents: %d\n sgt2 orig_nents: %d\n", sgt2->nents, sgt2->orig_nents);

    ktime_get_ts64(&start6);
    ret = dma_map_sgtable(dev, sgt3, DMA_TO_DEVICE, 0);
    ktime_get_ts64(&end6);
    if (ret)
    {
        pr_info("dma_map_sgtable failed\n");
        goto free_sgt;
    }
    ktime_get_ts64(&start7);
    ret = dma_map_sgtable(dev, sgt4, DMA_FROM_DEVICE, 0);
    ktime_get_ts64(&end7);
    if (ret)
    {
        pr_info("dma_map_sgtable failed\n");
        goto free_sgt;
    }

    pr_info("map3 time: %lld\n", timespec64_to_ns(&end6) - timespec64_to_ns(&start6));
    pr_info("map4 time: %lld\n", timespec64_to_ns(&end7) - timespec64_to_ns(&start7));

    ktime_get_ts64(&start10);
    dma_unmap_sgtable(dev, sgt3, DMA_TO_DEVICE, 0);
    ktime_get_ts64(&end10);
    ktime_get_ts64(&start11);
    dma_unmap_sgtable(dev, sgt4, DMA_FROM_DEVICE, 0);
    ktime_get_ts64(&end11);

    pr_info("unmap1 time: %lld\n", timespec64_to_ns(&end10) - timespec64_to_ns(&start10));
    pr_info("unmap2 time: %lld\n", timespec64_to_ns(&end11) - timespec64_to_ns(&start11));

    return;

free_sgt:
    sg_free_table(sgt3);
    sg_free_table(sgt4);

    return;
}

static void dsa_copy(void)
{
    struct idxd_desc_list *desc_list, *desc_entry, *desc_entry_temp;
    struct scatterlist *sg_src, *sg_dst, *src, *dst;
    int i;
    int cmp = 0;
    int poll = 0;
    int poll_entry = 0;
    int ret = 0;
    int fault = 0;
    int nents = 0;
    int loop_times = 0;
    int total_ents = 0;
    int left_ents = 0;
    int loop = 0;
    int time = 0;
    ///////////////////////
    // DSA_MEMCPY PROCESS
    ///////////////////////
    ktime_get_ts64(&start12);
    if (sgt1->nents > 1 && sgt2->nents > 1)
    {
        src = sgt1->sgl;
        dst = sgt2->sgl;

        total_ents = sgt1->nents > sgt2->nents ? sgt2->nents : sgt1->nents;

        loop_times = total_ents / wq->num_descs;
        loop_times += total_ents % wq->num_descs ? 1 : 0;

    next_loop:
        left_ents = total_ents - loop * wq->num_descs;

        nents = left_ents > wq->num_descs ? wq->num_descs : left_ents;

        for_each_2_sg(dst, src, sg_dst, sg_src, nents, i)
        {
            desc_list = (struct idxd_desc_list *)kzalloc(sizeof(struct idxd_desc_list), GFP_KERNEL);
            desc_list->desc = idxd_desc_dma_submit_memcpy(chan, sg_dma_address(sg_dst), sg_dma_address(sg_src), min_t(unsigned int, sg_dma_len(sg_src), sg_dma_len(sg_dst)), IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
            desc_list->completion = 0;
            list_add_tail(&desc_list->list, &idxd_desc_lists);
        }
        src = sg_src;
        dst = sg_dst;
    }
    else
    {
        nents = 1;
        desc_list = (struct idxd_desc_list *)kzalloc(sizeof(struct idxd_desc_list), GFP_KERNEL);
        desc_list->desc = idxd_desc_dma_submit_memcpy(chan, sg_dma_address(sgt2->sgl), sg_dma_address(sgt1->sgl), min_t(unsigned int, sg_dma_len(sgt1->sgl), sg_dma_len(sgt2->sgl)), IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
        desc_list->completion = 0;
        list_add_tail(&desc_list->list, &idxd_desc_lists);
    }
    ktime_get_ts64(&end12);
    // pr_info("nents: %d\n", nents);

    ktime_get_ts64(&start3);

    list_for_each_entry(desc_entry, &idxd_desc_lists, list)
    {
        ret = idxd_submit_desc(wq, desc_entry->desc);
        if (ret)
        {
            pr_info("submit failed\n");
            goto out;
        }
    }

    poll = 0;
    poll_entry = 0;

    while (poll++ < POLL_RETRY_MAX && poll_entry < nents)
    {
        list_for_each_entry_safe(desc_entry, desc_entry_temp, &idxd_desc_lists, list)
        {
            if (desc_entry->completion)
            {
                continue;
            }
            if (desc_entry->desc->completion->status == DSA_COMP_SUCCESS)
            {
                desc_entry->completion = 1;
                idxd_desc_complete(desc_entry->desc, IDXD_COMPLETE_NORMAL, 1);
                list_del(&desc_entry->list);
                kfree(desc_entry);
                poll_entry++;
            }
            else if (desc_entry->desc->completion->status > DSA_COMP_SUCCESS)
            {
                desc_entry->completion = 1;
                pr_info("after status: 0x%x\nfault info: 0x%x\n", desc_entry->desc->completion->status, desc_entry->desc->completion->fault_info);
                pr_info("result: 0x%x\ninvalid flag uint32: 0x%x\n", desc_entry->desc->completion->result, desc_entry->desc->completion->invalid_flags);
                poll_entry++;
            }
        }
        cpu_relax();
    }

    loop++;

    if (loop < loop_times)
    {
        if (loop == 1)
            time = timespec64_to_ns(&end12) - timespec64_to_ns(&start12);
        goto next_loop;
    }

    ktime_get_ts64(&start8);
    dma_unmap_sgtable(dev, sgt1, DMA_TO_DEVICE, 0);
    ktime_get_ts64(&end8);
    ktime_get_ts64(&start9);
    dma_unmap_sgtable(dev, sgt2, DMA_FROM_DEVICE, 0);
    ktime_get_ts64(&end9);

    ktime_get_ts64(&start13);
    sg_free_table(sgt1);
    sg_free_table(sgt2);
    ktime_get_ts64(&end13);

    ktime_get_ts64(&end3);

    if (poll >= POLL_RETRY_MAX || fault >= FAULT_RETRY_MAX)
    {
        pr_info("poll retry max\n");
    }

    pr_info("desc success\n");
    pr_info("poll: %d\nfault: %d\n", poll, fault);

    cmp = memcmp(kmalloc_area, kmalloc_area2, NPAGES * PAGE_SIZE);
    cmp ? pr_info("DSA FAIL\n") : pr_info("DSA Success\n");
    // pr_info("1st page %s\n", page_address(vmalloc_to_page(kmalloc_area + 20 * PAGE_SIZE)));
    // pr_info("2nd page %s\n", page_address(vmalloc_to_page(vmalloc_area2 + 20 * PAGE_SIZE)));

    // memmove with CPU
    pr_info("memmove start\n");

    ktime_get_ts64(&start4);
    for (int i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        memmove(page_address(virt_to_page(kmalloc_area4 + i)), page_address(virt_to_page(kmalloc_area3 + i)), PAGE_SIZE);
    }
    ktime_get_ts64(&end4);

    cmp = memcmp(kmalloc_area3, kmalloc_area4, PAGE_SIZE * NPAGES);
    cmp ? pr_info("memmove copy fail\n") : pr_info("memmove copy success\n");

    pr_info("nents: %d\n", nents);

    pr_info("src map time1: %lld\n", timespec64_to_ns(&end) - timespec64_to_ns(&start));
    pr_info("dest map time2: %lld\n", timespec64_to_ns(&end5) - timespec64_to_ns(&start5));
    pr_info("descs alloc time: %d\n", time);
    pr_info("desc alloc time 2: %lld\n", timespec64_to_ns(&end12) - timespec64_to_ns(&start12));
    pr_info("DSA end to end time: %lld\n", timespec64_to_ns(&end3) - timespec64_to_ns(&start2));
    pr_info("DSA memmove time3: %lld\n", timespec64_to_ns(&start8) - timespec64_to_ns(&start3));
    pr_info("sg_table free time: %lld\n", timespec64_to_ns(&end13) - timespec64_to_ns(&start13));
    pr_info("memmove time4: %lld\n", timespec64_to_ns(&end4) - timespec64_to_ns(&start4));

    ///////////////////////
    // kmalloc mapping test
    ///////////////////////

    // dma_unmap_single(device->dev, src1, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);
    // dma_unmap_single(device->dev, dst1, NPAGES * PAGE_SIZE, DMA_BIDIRECTIONAL);

    pr_info("unmap src time: %lld\n", timespec64_to_ns(&end8) - timespec64_to_ns(&start8));
    pr_info("unmap dest time: %lld\n", timespec64_to_ns(&end9) - timespec64_to_ns(&start9));

out:

    return;
}

static int __init
my_init(void)
{
    INIT_LIST_HEAD(&idxd_desc_lists);
    pr_info("mykmap module loaded\n");
    int init_result = init_region();
    if (init_result < 0)
    {
        return init_result;
    }
    pr_info("init_region success\n");
    int dsa_result = init_dsa();
    if (dsa_result < 0)
    {
        return dsa_result;
    }
    pr_info("init_dsa success\n");
    test();
    ktime_get_ts64(&start2);
    vmalloc_to_sgtable();
    // pr_info("vmalloc_to_sgtable success\n");
    sgtable_to_dma_map();
    // pr_info("sgtable_to_dma_map success\n");
    dsa_copy();
    // pr_info("dsa_copy success\n");

    return 0;
}

static void exit_module(void)
{
    int i;

    /* TODO 1/3: clear reservation on pages and free mem.*/
    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        ClearPageReserved(virt_to_page(kmalloc_area + i));
        ClearPageReserved(virt_to_page(kmalloc_area2 + i));
        ClearPageReserved(virt_to_page(kmalloc_area3 + i));
        ClearPageReserved(virt_to_page(kmalloc_area4 + i));
        ClearPageReserved(virt_to_page(kmalloc_area5 + i));
        ClearPageReserved(virt_to_page(kmalloc_area6 + i));
    }

    kfree(kmalloc_area);
    kfree(kmalloc_area2);
    kfree(kmalloc_area3);
    kfree(kmalloc_area4);
    kfree(kmalloc_area5);
    kfree(kmalloc_area6);

    sg_free_table(sgt3);
    sg_free_table(sgt4);
}

static void __exit my_exit(void)
{
    list_del_init(&idxd_desc_lists);
    exit_module();
    dma_release_channel(chan);
    pr_info("myvmap module unloaded\n");
}

module_init(my_init);
module_exit(my_exit);
