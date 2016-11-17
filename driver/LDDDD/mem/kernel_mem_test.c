#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/bcd.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

void* kmalloc_p = NULL;
void* vmalloc_p = NULL;
	
static int mem_test_init(void)
{
	kmalloc_p = kmalloc(64, GFP_KERNEL);
	if(kmalloc_p == NULL)
	{
		printk("kmalloc() failed\n");
		return -1;
	}
	
	vmalloc_p = vmalloc(2048);
	if(vmalloc_p == NULL)
	{
		printk("vmalloc() failed\n");
		kfree(kmalloc_p);
		return -1;
	}
	
	printk("kmalloc_p = 0x%08x\n", (unsigned int)kmalloc_p);
	printk("vmalloc_p = 0x%08x\n", (unsigned int)vmalloc_p);
	
	return 0;
}

static void mem_test_exit(void)
{
	vfree(vmalloc_p);
	kfree(kmalloc_p);
}

module_init(mem_test_init);
module_exit(mem_test_exit);
MODULE_LICENSE("GPL");