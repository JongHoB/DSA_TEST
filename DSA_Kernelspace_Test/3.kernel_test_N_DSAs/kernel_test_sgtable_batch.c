
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
#include <linux/kthread.h>
#include <linux/wait.h>

#include "kernel_test.h"

MODULE_DESCRIPTION("DSA in KERNEL");
MODULE_AUTHOR("Jongho Baik");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(IDXD);

LIST_HEAD(idxd_desc_lists);
LIST_HEAD(idxd_wqs);

static DECLARE_WAIT_QUEUE_HEAD(desc_wait);
static struct task_struct *dsa_desc_complete_thread;

static int enabled_wqs = 0;
static int total_entries = 0;
static bool thread_execution = false;
static bool thread_wakeup = false;

struct idxd_device *idxd_device;
struct device *dev;

/* pointer to the vmalloc'd area, rounded up to a page boundary */
static char *vmalloc_area;
static char *vmalloc_area2;
static char *vmalloc_area3;
static char *vmalloc_area4;
static char *vmalloc_area5;
static char *vmalloc_area6;

struct sg_table **src_sgts;
struct sg_table **dst_sgts;

static int completion_status = 0;

struct timespec64 a, b, c, d, e, f, g, h, i, j, k, l, m, n;

u32 *arr;

static void random_arr(void)
{
    arr = kmalloc(sizeof(u32) * NPAGES, GFP_KERNEL);

    for (int i = 0; i < NPAGES; i++)
    {
        arr[i] = i;
    }

    for (int i = 0; i < NPAGES; i++)
    {
        u32 tmp = (get_random_u32() >> 1) % NPAGES;
        u32 tmp2 = arr[i];
        arr[i] = arr[tmp];
        arr[tmp] = tmp2;
    }

    pr_info("src \n");
    for (int i = 0; i < NPAGES; i++)
    {
        unsigned long pfn = vmalloc_to_pfn(vmalloc_area + arr[i] * PAGE_SIZE);
        // pr_info("%lu\n", pfn);

        // check contiguity
        if (i > 0)
        {
            if (pfn == vmalloc_to_pfn(vmalloc_area + arr[i - 1] * PAGE_SIZE) + 1)
            {
                pr_info("%d %lu %lu\n", i, pfn, vmalloc_to_pfn(vmalloc_area + arr[i - 1] * PAGE_SIZE));
            }
        }
    }
    pr_info("dest \n");
    for (int i = 0; i < NPAGES; i++)
    {
        unsigned long pfn = vmalloc_to_pfn(vmalloc_area2 + arr[i] * PAGE_SIZE);
        // pr_info("%lu\n", pfn);
        if (i > 0)
        {
            if (pfn == vmalloc_to_pfn(vmalloc_area + arr[i - 1] * PAGE_SIZE) + 1)
            {
                pr_info("%d %lu %lu\n", i, pfn, vmalloc_to_pfn(vmalloc_area + arr[i - 1] * PAGE_SIZE));
            }
        }
    }

    return;
}

static int init_dsa(void)
{

    struct pci_dev *pci_dev = NULL;
    struct pci_device_id pci_device_id = {PCI_DEVICE_DATA(INTEL, DSA_SPR0, NULL)};

    while ((pci_dev = pci_get_device(pci_device_id.vendor, pci_device_id.device, pci_dev)) != NULL)
    {
        struct idxd_device *idxd_device = pci_get_drvdata(pci_dev);
        if (idxd_device && idxd_device->state == IDXD_DEV_ENABLED && test_bit(IDXD_FLAG_CONFIGURABLE, &idxd_device->flags))
        {
            for (int i = 0; i < idxd_device->max_wqs; i++)
            {
                if (idxd_device->wqs[i]->state == IDXD_WQ_ENABLED && is_idxd_wq_dmaengine(idxd_device->wqs[i]) && is_idxd_wq_kernel(idxd_device->wqs[i]) && device_pasid_enabled(idxd_device->wqs[i]->idxd))
                {
                    struct enabled_idxd_wqs *enabled_wq = (struct enabled_idxd_wqs *)kmalloc(sizeof(struct enabled_idxd_wqs), GFP_KERNEL);
                    enabled_wq->enabled_wq_num = enabled_wqs++;
                    // enabled_wq->dev = &pci_dev->dev;
                    enabled_wq->wq = idxd_device->wqs[i];
                    // enabled_wq->chan = dma_get_slave_channel(&idxd_device->wqs[i]->idxd_chan->chan);
                    dma_get_slave_channel(&idxd_device->wqs[i]->idxd_chan->chan);
                    pr_info("dma name: %s\n", dma_chan_name(&idxd_device->wqs[i]->idxd_chan->chan));
                    list_add_tail(&enabled_wq->list, &idxd_wqs);
                }
            }
        }
    }
    // pr_info("wq num_descs: %d\n", wq->num_descs);

    if (enabled_wqs == 0)
    {
        pr_info("No enabled wqs\n");
        return -ENODEV;
    }

    random_arr();
    pr_info("enabled wqs: %d\n", enabled_wqs);

    return 0;
}

// for vmalloc region
static int init_region(void)
{
    int ret = 0;
    int i;

    pr_info("transfer size: %ld\n", NPAGES * PAGE_SIZE);

    /* TODO 1/6: allocate NPAGES using kmalloc */
    vmalloc_area = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area2 = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area3 = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area4 = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area5 = (char *)vmalloc(NPAGES * PAGE_SIZE);
    vmalloc_area6 = (char *)vmalloc(NPAGES * PAGE_SIZE);

    if (vmalloc_area == NULL || vmalloc_area2 == NULL || vmalloc_area3 == NULL || vmalloc_area4 == NULL || vmalloc_area5 == NULL || vmalloc_area6 == NULL)
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
        SetPageReserved(vmalloc_to_page(vmalloc_area5 + i));
        SetPageReserved(vmalloc_to_page(vmalloc_area6 + i));
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
        pfn = vmalloc_to_pfn(vmalloc_area5 + i);
        // pr_info("area 5: %lu\n", pfn);
        pfn = vmalloc_to_pfn(vmalloc_area6 + i);
        // pr_info("area 6: %lu\n", pfn);
    }

    for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        // Write kmalloc in each page
        sprintf(vmalloc_area + i, "vmalloc %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area2 + i, "vmalloc2 %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area3 + i, "vmalloc3 %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area4 + i, "vmalloc4 %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area5 + i, "vmalloc5 %ld", i / PAGE_SIZE);
        sprintf(vmalloc_area6 + i, "vmalloc6 %ld", i / PAGE_SIZE);

        // Last of the String is \0
        // After the null i will fill it with 1
        // So i can check if the data is written correctly
        memset(vmalloc_area + i + strlen(vmalloc_area + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area + i) - 1);
        memset(vmalloc_area2 + i + strlen(vmalloc_area2 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area2 + i) - 1);
        memset(vmalloc_area3 + i + strlen(vmalloc_area3 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area3 + i) - 1);
        memset(vmalloc_area4 + i + strlen(vmalloc_area4 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area4 + i) - 1);
        memset(vmalloc_area5 + i + strlen(vmalloc_area5 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area5 + i) - 1);
        memset(vmalloc_area6 + i + strlen(vmalloc_area6 + i) + 1, '0', PAGE_SIZE - strlen(vmalloc_area6 + i) - 1);
    }

    return 0;

out_vfree:
    vfree(vmalloc_area);
    vfree(vmalloc_area2);
    vfree(vmalloc_area3);
    vfree(vmalloc_area4);
    vfree(vmalloc_area5);
    vfree(vmalloc_area6);

    return ret;
}

static void vmalloc_to_sgtable(void)
{
    // make vmalloc area to sgtable
    int j, idx;
    struct scatterlist *sg, *prev_sg;
    struct page *page;
    src_sgts = kmalloc(sizeof(struct sg_table *) * enabled_wqs, GFP_KERNEL);
    dst_sgts = kmalloc(sizeof(struct sg_table *) * enabled_wqs, GFP_KERNEL);

    for (int i = 0; i < enabled_wqs; i++)
    {
        src_sgts[i] = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
        dst_sgts[i] = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
    }

    for (int i = 0; i < enabled_wqs; i++)
    {
        int nents = NPAGES % enabled_wqs == 0 ? NPAGES / enabled_wqs : NPAGES / enabled_wqs + 1;

        if (sg_alloc_table(src_sgts[i], nents, GFP_KERNEL) || sg_alloc_table(dst_sgts[i], nents, GFP_KERNEL))
        {
            pr_info("sg_alloc_table failed\n");
            goto free_sgt;
        }
    }

    idx = 0;
    for (int i = 0; i < enabled_wqs; i++)
    {
        for_each_sgtable_sg(src_sgts[i], sg, j)
        {
            if (idx + j >= NPAGES)
                break;
            page = vmalloc_to_page(vmalloc_area + arr[idx + j] * PAGE_SIZE);
            if (!page)
            {
                goto free_sgt;
            }
            sg_set_page(sg, page, PAGE_SIZE, 0);
            prev_sg = sg;
        }

        sg_mark_end(prev_sg);

        for_each_sgtable_sg(dst_sgts[i], sg, j)
        {
            if (idx >= NPAGES)
                break;
            page = vmalloc_to_page(vmalloc_area2 + arr[idx++] * PAGE_SIZE);
            if (!page)
            {
                goto free_sgt;
            }
            sg_set_page(sg, page, PAGE_SIZE, 0);
            prev_sg = sg;
        }

        sg_mark_end(prev_sg);
    }
    // pr_info("idx: %d\n", idx);
    // pr_info("src nents: %d origin: %d \n", src_sgts[0]->nents, src_sgts[0]->orig_nents);
    // pr_info("dst nents: %d origin: %d \n", dst_sgts[0]->nents, dst_sgts[0]->orig_nents);

    return;

free_sgt:
    for (int i = 0; i < enabled_wqs; i++)
    {
        sg_free_table(src_sgts[i]);
        sg_free_table(dst_sgts[i]);
    }

    return;
}

static void sgtable_to_dma_map(void)
{
    int ret;
    struct enabled_idxd_wqs *enabled_wq;

    // ktime_get_ts64(&start);
    list_for_each_entry(enabled_wq, &idxd_wqs, list)
    {
        // pr_info("enabled_wq_num: %d\n", enabled_wq->enabled_wq_num);
        // ktime_get_ts64(&start);
        ret = dma_map_sgtable(wq_to_dev(enabled_wq->wq), src_sgts[enabled_wq->enabled_wq_num], DMA_TO_DEVICE, 0);
        if (ret)
        {
            pr_info("dma_map_sgtable failed\n");
            return;
        }
        // ktime_get_ts64(&end);
        ret = dma_map_sgtable(wq_to_dev(enabled_wq->wq), dst_sgts[enabled_wq->enabled_wq_num], DMA_FROM_DEVICE, 0);
        if (ret)
        {
            pr_info("dma_map_sgtable failed\n");
            return;
        }
        // ktime_get_ts64(&end);
    }
    // ktime_get_ts64(&end);
    return;
}

static int dsa_desc_complete(void *arg)
{
    pr_info("dsa_desc_complete_thread started\n");
    wait_event_interruptible(desc_wait, thread_execution);
    thread_wakeup = true;
    // pr_info("dsa_desc_complete_thread woken up\n");

    struct idxd_desc_list *desc_entry, *desc_entry_temp;
    int poll_entry = 0;
    bool completion_error = false;

    while (!kthread_should_stop())
    {
        // pr_info("1\n");
        while (!kthread_should_stop() && poll_entry < total_entries && !list_empty(&idxd_desc_lists))
        {
            // pr_info("2\n");
            list_for_each_entry_safe(desc_entry, desc_entry_temp, &idxd_desc_lists, list)
            {
                if (desc_entry->completion)
                {
                    if (!thread_execution)
                    {
                        // pr_info("1\n");
                        list_del(&desc_entry->list);
                        kfree(desc_entry);
                    }
                    continue;
                }
                if (desc_entry->desc->completion->status == DSA_COMP_SUCCESS)
                {
                    desc_entry->desc->txd.cookie = 1;
                    desc_entry->completion = 1;
                    idxd_desc_complete(desc_entry->desc, IDXD_COMPLETE_NORMAL, 1);
                    poll_entry++;

                    if (desc_entry->batch_info)
                    {
                        // pr_info("3\n");
                        dma_unmap_single(wq_to_dev(desc_entry->desc->wq), desc_entry->batch_info->compls_addr, desc_entry->batch_info->batch_compls_size, DMA_FROM_DEVICE);
                        dma_unmap_single(wq_to_dev(desc_entry->desc->wq), desc_entry->batch_info->hw_descs_addr, desc_entry->batch_info->batch_hw_descs_size, DMA_TO_DEVICE);

                        kfree(desc_entry->batch_info->sub_compl);
                        kfree(desc_entry->batch_info->sub_hw_descs);
                        kfree(desc_entry->batch_info);
                    }

                    if (!thread_execution)
                    {
                        // pr_info("4\n");
                        list_del(&desc_entry->list);
                        kfree(desc_entry);
                    }
                }
                else if (desc_entry->desc->completion->status > DSA_COMP_SUCCESS)
                {
                    completion_error = true;
                    desc_entry->completion = 1;
                    idxd_desc_complete(desc_entry->desc, IDXD_COMPLETE_ABORT, 1);
                    pr_info("after status: 0x%x\nfault info: 0x%x\n", desc_entry->desc->completion->status, desc_entry->desc->completion->fault_info);
                    pr_info("result: 0x%x\ninvalid flag uint32: 0x%x\n", desc_entry->desc->completion->result, desc_entry->desc->completion->invalid_flags);
                    poll_entry++;

                    if (desc_entry->batch_info)
                    {
                        dma_unmap_single(wq_to_dev(desc_entry->desc->wq), desc_entry->batch_info->compls_addr, desc_entry->batch_info->batch_compls_size, DMA_FROM_DEVICE);
                        dma_unmap_single(wq_to_dev(desc_entry->desc->wq), desc_entry->batch_info->hw_descs_addr, desc_entry->batch_info->batch_hw_descs_size, DMA_TO_DEVICE);

                        kfree(desc_entry->batch_info->sub_compl);
                        kfree(desc_entry->batch_info->sub_hw_descs);
                        kfree(desc_entry->batch_info);
                    }

                    if (!thread_execution)
                    {
                        list_del(&desc_entry->list);
                        kfree(desc_entry);
                    }
                }
            }
            cpu_relax();
        }
        if (!thread_execution && list_empty(&idxd_desc_lists))
        {
            completion_status = completion_error ? 2 : 1;
            // pr_info("thread_execution is false\n");
            thread_wakeup = false;
            wait_event_interruptible(desc_wait, thread_execution);
            // pr_info("thread_execution is true\n");
        }
        cpu_relax();
    }

    pr_info("dsa_desc_complete_thread stopped\n");
    return 0;
}

static void dsa_copy(void)
{
    ///////////////////////
    // DSA_MEMCPY PROCESS
    ///////////////////////
    struct idxd_desc_list *desc_list;
    struct batch_task *batch_task = NULL;
    struct enabled_idxd_wqs *enabled_wq;
    int cmp = 0;
    int cur_ent = 0;
    int max_ents = 0;
    struct scatterlist **sg_srcs = kzalloc(sizeof(struct scatterlist *) * enabled_wqs, GFP_KERNEL);
    struct scatterlist **sg_dsts = kzalloc(sizeof(struct scatterlist *) * enabled_wqs, GFP_KERNEL);
    int *descs_entries = kzalloc(sizeof(int) * enabled_wqs, GFP_KERNEL);

    ktime_get_ts64(&m);

    list_for_each_entry(enabled_wq, &idxd_wqs, list)
    {
        sg_srcs[enabled_wq->enabled_wq_num] = src_sgts[enabled_wq->enabled_wq_num]->sgl;
        sg_dsts[enabled_wq->enabled_wq_num] = dst_sgts[enabled_wq->enabled_wq_num]->sgl;

        int total_nents = (src_sgts[enabled_wq->enabled_wq_num]->nents) > (dst_sgts[enabled_wq->enabled_wq_num]->nents) ? (dst_sgts[enabled_wq->enabled_wq_num]->nents) : (src_sgts[enabled_wq->enabled_wq_num]->nents);
        int nents = total_nents / (enabled_wq->wq->idxd->max_batch_size);
        nents += total_nents % (enabled_wq->wq->idxd->max_batch_size) ? 1 : 0;

        descs_entries[enabled_wq->enabled_wq_num] = nents;

        max_ents = max_t(int, max_ents, nents);
    }
    total_entries = max_ents * enabled_wqs;
    // pr_info("total_entries: %d\n", total_entries);
    ktime_get_ts64(&h);
    while (cur_ent < max_ents)
    {
        list_for_each_entry(enabled_wq, &idxd_wqs, list)
        {
            if (cur_ent < descs_entries[enabled_wq->enabled_wq_num])
            {
                int total_nents = (src_sgts[enabled_wq->enabled_wq_num]->nents) > (dst_sgts[enabled_wq->enabled_wq_num]->nents) ? (dst_sgts[enabled_wq->enabled_wq_num]->nents) : (src_sgts[enabled_wq->enabled_wq_num]->nents);

                // pr_info("total_nents: %d\n", total_nents);

                if (total_nents == 1)
                {
                    desc_list = (struct idxd_desc_list *)kzalloc(sizeof(struct idxd_desc_list), GFP_KERNEL);
                    desc_list->desc = idxd_desc_dma_submit_memcpy(&enabled_wq->wq->idxd_chan->chan, sg_dma_address(sg_dsts[enabled_wq->enabled_wq_num]), sg_dma_address(sg_srcs[enabled_wq->enabled_wq_num]), min_t(unsigned int, sg_dma_len(sg_srcs[enabled_wq->enabled_wq_num]), sg_dma_len(sg_dsts[enabled_wq->enabled_wq_num])), IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
                    desc_list->completion = 0;
                }
                else
                {
                    int left_ents = total_nents - cur_ent * enabled_wq->wq->idxd->max_batch_size;
                    int batch_size = left_ents > enabled_wq->wq->idxd->max_batch_size ? enabled_wq->wq->idxd->max_batch_size : left_ents;
                    ktime_get_ts64(&k);
                    batch_task = idxd_desc_dma_submit_memcpy_sg(&enabled_wq->wq->idxd_chan->chan, sg_dsts[enabled_wq->enabled_wq_num], sg_srcs[enabled_wq->enabled_wq_num], batch_size, IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_CC | IDXD_OP_FLAG_BOF);
                    ktime_get_ts64(&l);
                    sg_dsts[enabled_wq->enabled_wq_num] = batch_task->next_sg_dst;
                    sg_srcs[enabled_wq->enabled_wq_num] = batch_task->next_sg_src;
                    desc_list = batch_task->desc_list;
                }

                list_add_tail(&desc_list->list, &idxd_desc_lists);

                int ret = idxd_submit_desc(enabled_wq->wq, desc_list->desc);
                if (ret)
                {
                    pr_info("submit failed\n");
                }

                if (!thread_execution)
                {
                    thread_execution = true;
                    wake_up(&desc_wait);
                    pr_info("wake up called\n");
                }

                if (batch_task)
                {
                    kfree(batch_task);
                }
            }
        }
        cur_ent++;
        // pr_info("cur_ent: %d\n", cur_ent++);
    }
    ///////
    // pr_info("descs submitted\n");
    ktime_get_ts64(&i);
    while (!thread_wakeup)
    {
        cpu_relax();
    }

    thread_execution = false;
    // pr_info("thread sleep\n");

    while (!completion_status)
    {
        cpu_relax();
    }

    if (completion_status == 2)
    {
        pr_info("DSA copy failed\n");
    }
    ktime_get_ts64(&j);
    list_for_each_entry(enabled_wq, &idxd_wqs, list)
    {
        dma_unmap_sgtable(wq_to_dev(enabled_wq->wq), src_sgts[enabled_wq->enabled_wq_num], DMA_TO_DEVICE, 0);
        dma_unmap_sgtable(wq_to_dev(enabled_wq->wq), dst_sgts[enabled_wq->enabled_wq_num], DMA_FROM_DEVICE, 0);

        ktime_get_ts64(&e);
        sg_free_table(src_sgts[enabled_wq->enabled_wq_num]);
        ktime_get_ts64(&f);
        sg_free_table(dst_sgts[enabled_wq->enabled_wq_num]);
        ktime_get_ts64(&g);
    }

    ktime_get_ts64(&b);

    pr_info("desc success\n");

    cmp = memcmp(vmalloc_area, vmalloc_area2, NPAGES * PAGE_SIZE);
    cmp ? pr_info("DSA FAIL\n") : pr_info("DSA Success\n");

    // memmove with CPU
    pr_info("memmove start\n");

    ktime_get_ts64(&c);
    for (int i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
    {
        memmove(page_address(vmalloc_to_page(vmalloc_area4 + i)), page_address(vmalloc_to_page(vmalloc_area3 + i)), PAGE_SIZE);
    }
    ktime_get_ts64(&d);

    cmp = memcmp(vmalloc_area3, vmalloc_area4, PAGE_SIZE * NPAGES);
    cmp ? pr_info("memmove copy fail\n") : pr_info("memmove copy success\n");

    pr_info("sg_table         free  time: %lld\n", timespec64_to_ns(&f) - timespec64_to_ns(&e));
    pr_info("sg_table         free time2: %lld\n", timespec64_to_ns(&g) - timespec64_to_ns(&f));
    pr_info("End to                  End: %lld\n", timespec64_to_ns(&b) - timespec64_to_ns(&a));
    pr_info("memmove               time4: %lld\n", timespec64_to_ns(&d) - timespec64_to_ns(&c));

    pr_info("before create          time: %lld\n", timespec64_to_ns(&h) - timespec64_to_ns(&a));
    pr_info("nents calc             time: %lld\n", timespec64_to_ns(&h) - timespec64_to_ns(&m));
    pr_info("desc create + submit   time: %lld\n", timespec64_to_ns(&i) - timespec64_to_ns(&h));
    pr_info("desc create            time: %lld\n", timespec64_to_ns(&l) - timespec64_to_ns(&k));

    pr_info("after submit -complete time: %lld\n", timespec64_to_ns(&j) - timespec64_to_ns(&i));
    pr_info("unmap+ .          free time: %lld\n", timespec64_to_ns(&b) - timespec64_to_ns(&j));

    return;
}

static int __init
my_init(void)
{
    dsa_desc_complete_thread = kthread_run(dsa_desc_complete, NULL, "dsa_desc_complete_thread");
    if (IS_ERR(dsa_desc_complete_thread))
    {
        pr_info("kthread_run failed\n");
        return -1;
    }
    INIT_LIST_HEAD(&idxd_desc_lists);
    INIT_LIST_HEAD(&idxd_wqs);
    // pr_info("mykmap module loaded\n");
    int init_result = init_region();
    if (init_result < 0)
    {
        return init_result;
    }
    // pr_info("init_region success\n");
    int dsa_result = init_dsa();
    if (dsa_result < 0)
    {
        return dsa_result;
    }
    // pr_info("init_dsa success\n");
    ktime_get_ts64(&a);
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
        ClearPageReserved(vmalloc_to_page(vmalloc_area + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area2 + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area3 + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area4 + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area5 + i));
        ClearPageReserved(vmalloc_to_page(vmalloc_area6 + i));
    }

    vfree(vmalloc_area);
    vfree(vmalloc_area2);
    vfree(vmalloc_area3);
    vfree(vmalloc_area4);
    vfree(vmalloc_area5);
    vfree(vmalloc_area6);

    // pr_info("exit_module success\n");
}

static void release_channels(void)
{
    struct enabled_idxd_wqs *enabled_wq;
    list_for_each_entry(enabled_wq, &idxd_wqs, list)
    {
        dma_release_channel(&enabled_wq->wq->idxd_chan->chan);
    }
}

static void __exit my_exit(void)
{
    kthread_stop(dsa_desc_complete_thread);
    release_channels();
    list_del_init(&idxd_desc_lists);
    list_del_init(&idxd_wqs);
    exit_module();
    // pr_info("myvmap module unloaded\n");
}

module_init(my_init);
module_exit(my_exit);
