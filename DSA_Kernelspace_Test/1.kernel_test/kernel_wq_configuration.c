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

MODULE_DESCRIPTION("DSA WQ configuration test module");
MODULE_AUTHOR("Jongho Baik");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(IDXD);

static struct dma_chan *chan = NULL;
static struct idxd_wq *wq;
struct idxd_device *idxd_device;

static int idxd_enable_system_pasid(struct idxd_device *idxd)
{
    struct pci_dev *pdev = idxd->pdev;
    struct device *dev = &pdev->dev;
    struct iommu_domain *domain;
    ioasid_t pasid;
    int ret;

    /*
     * Attach a global PASID to the DMA domain so that we can use ENQCMDS
     * to submit work on buffers mapped by DMA API.
     */
    domain = iommu_get_domain_for_dev(dev);
    if (!domain)
        return -EPERM;

    pasid = iommu_alloc_global_pasid(dev);
    if (pasid == IOMMU_PASID_INVALID)
        return -ENOSPC;
    pr_info("pasid: %d\n", pasid);
    /*
     * DMA domain is owned by the driver, it should support all valid
     * types such as DMA-FQ, identity, etc.
     */
    ret = iommu_attach_device_pasid(domain, dev, pasid);
    pr_info("ret: %d\n", ret);
    if (ret)
    {
        dev_err(dev, "failed to attach device pasid %d, domain type %d",
                pasid, domain->type);
        iommu_free_global_pasid(pasid);
        return ret;
    }

    /* Since we set user privilege for kernel DMA, enable completion IRQ */
    idxd_set_user_intr(idxd, 1);
    idxd->pasid = pasid;

    return pasid;
}

static void idxd_disable_system_pasid(struct idxd_device *idxd)
{
    struct pci_dev *pdev = idxd->pdev;
    struct device *dev = &pdev->dev;
    struct iommu_domain *domain;

    domain = iommu_get_domain_for_dev(dev);
    if (!domain)
        return;

    iommu_detach_device_pasid(domain, dev, idxd->pasid);
    iommu_free_global_pasid(idxd->pasid);

    idxd_set_user_intr(idxd, 0);
    idxd->sva = NULL;
    idxd->pasid = IOMMU_PASID_INVALID;
}
static int
init_dsa(void)
{

    struct pci_dev *pci_dev = NULL;
    struct pci_dev *pci_dev_list[8];
    struct device *dev = NULL;
    struct pci_device_id pci_device_id = {PCI_DEVICE_DATA(INTEL, DSA_SPR0, NULL)};
    int device_count = 0;
    unsigned int pasid;
    struct iommu_sva *sva;
    int rc;

    while ((pci_dev = pci_get_device(pci_device_id.vendor, pci_device_id.device, pci_dev)) != NULL)
    {
        pci_dev_list[device_count] = pci_dev;
        device_count++;
        struct idxd_device *idxd_device = pci_get_drvdata(pci_dev);
        pr_info("device: %d\n", device_count);
        pr_info("wq ats support:%d\n", idxd_device->hw.wq_cap.wq_ats_support);
        pr_info("wq prs support:%d\n", idxd_device->hw.wq_cap.wq_prs_support);
        if (device_count >= 8)
            break;
    }

    if (device_count < 8)
    {
        pr_err("could not find 8 DSA devices\n");
        return -1;
    }

    dev = &pci_dev_list[2]->dev;

    idxd_device = pci_get_drvdata(pci_dev_list[2]);

    if (!idxd_device)
    {
        pr_err("could not get idxd device\n");
        return -1;
    }
    if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd_device->flags))
    {
        pr_err("idxd device is not configurable\n");
    }
    if (idxd_device->wqs[1]->state != IDXD_WQ_ENABLED)
    {
        pr_err("idxd wq 1 is not enabled\n");
        return -1;
    }

    wq = idxd_device->wqs[1];

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

    pr_info("current pasid, %d\n", wq->idxd->pasid);

    // if (wq->client_count > 0)
    // {
    //     dma_release_channel(&wq->idxd_chan->chan);
    // }

    // pr_info("wq num_descs: %d\n", wq->num_descs);

    // // dma infos
    pr_info("dma device name: %s\n", dma_chan_name(&wq->idxd_chan->chan));

    idxd_disable_system_pasid(idxd_device);

    rc = idxd_enable_system_pasid(idxd_device);
    if (rc)
        dev_warn(dev, "No in-kernel DMA with PASID. %d\n", rc);
    else
        set_bit(IDXD_FLAG_PASID_ENABLED, &idxd_device->flags);

    pr_info("current pasid, %d\n", wq->idxd->pasid);
    // pr_info("wq flags: 0x%x\n", wq->flags);
    // set_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);
    // set_bit(WQ_FLAG_PRS_DISABLE, &wq->flags);
    // clear_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);
    // clear_bit(WQ_FLAG_PRS_DISABLE, &wq->flags);
    // pr_info("after wq flags: 0x%x\n", wq->flags);

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

static int __init
my_init(void)
{
    int dsa_result = init_dsa();
    if (dsa_result < 0)
    {
        return dsa_result;
    }

    return 0;
}

static void __exit my_exit(void)
{
}

module_init(my_init);
module_exit(my_exit);