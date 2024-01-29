#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "kernel_test.h"

int fragmenter_init(void)
{
    if (order < 0 || order > 11)
    {
        printk(KERN_INFO "Invalid order value\n");
        return -1;
    }
    if (fragmentation_score < 0 || fragmentation_score > 100)
    {
        printk(KERN_INFO "Invalid Fragmentation Score value\n");
        return -1;
    }
    if (order != 0)
    {

        printk(KERN_INFO "Starting fragmenter\n");
        create_fragments(NULL);
    }
    else
    {
        score_printer();
    }
    return 0;
}
void fragmenter_exit(void)
{
    printk(KERN_INFO "Releasing all fragments\n");
    release_fragments();
    printk(KERN_INFO "Released all fragments\n");
}
module_init(fragmenter_init);
module_exit(fragmenter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
    "Physical Memory Fragmenter, exhausts contiguous physical memory (from a particular order)");
MODULE_AUTHOR("Jongho Baik");
