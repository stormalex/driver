#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>

struct key_desc{
	int irq_num;
	char *name;
	unsigned int pin;
	unsigned int key_val;
};

struct key_desc keys[4]={
	{IRQ_EINT4, "key_1", S3C2410_GPF(4), KEY_A},
	{IRQ_EINT5, "key_2", S3C2410_GPF(5), KEY_B},
	{IRQ_EINT6, "key_3", S3C2410_GPF(6), KEY_C},
	{IRQ_EINT7, "key_4", S3C2410_GPF(7), KEY_D},
};

static struct input_dev *button_dev = NULL;
static struct timer_list button_timer;
static int reg_irq = 0;
struct key_desc *key = NULL;

static irqreturn_t button_interrupt(int irq, void *key_i)
{
	key = key_i;
	mod_timer(&button_timer, jiffies+1);
	return IRQ_HANDLED;
}

static void button_tiemer_function(unsigned long data)
{
	struct key_desc *key_pass = key;
	unsigned int value;

	if(!key_pass)
		return;
	value = s3c2410_gpio_getpin(key_pass->pin);
	
	if(value)
	{
		input_report_key(button_dev, KEY_A, 0);
		input_sync(button_dev);
	}
	else
	{
		input_report_key(button_dev, KEY_A, 1);
		input_sync(button_dev);
	}
}

static int button_open(struct input_dev *dev)
{
	int ret = 0;
	for(reg_irq = 0; reg_irq < 4; reg_irq++)
	{ 
		if (request_irq(keys[reg_irq].irq_num, button_interrupt, (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING), keys[reg_irq].name, &keys[reg_irq]))
		{
			printk(KERN_ERR "button.c: Can't allocate irq %d\n", keys[reg_irq].irq_num);
			reg_irq--;
			ret = -EBUSY;
			goto err_free_irq;
		}
	}

	return 0;

err_free_irq:
	for(; reg_irq > 0; reg_irq--)
		free_irq(keys[reg_irq].irq_num, &keys[reg_irq]);

	reg_irq = 0;
	
	return ret;
}

static void button_close(struct input_dev *dev)
{
	reg_irq = ((sizeof(keys))/(sizeof(struct key_desc))) - 1;

	for(; reg_irq >= 0; reg_irq--)
	{
		free_irq(keys[reg_irq].irq_num, &keys[reg_irq]);
	}

	reg_irq = 0;
}


static int button_init(void)
{
	int error;

	button_dev = input_allocate_device();
	if (!button_dev)
	{
		printk(KERN_ERR "button.c: Not enough memory\n");
		error = -ENOMEM;
		return error;
	}

	set_bit(EV_KEY, button_dev->evbit);		//设置可以产生KEY事件
	set_bit(KEY_A, button_dev->keybit);		//设置可以产生哪些按键
	set_bit(KEY_B, button_dev->keybit);
	set_bit(KEY_C, button_dev->keybit);
	set_bit(KEY_D, button_dev->keybit);

	//button_dev->evbit[0] = BIT_MASK(EV_KEY);
	//button_dev->keybit[BIT_WORD(KEY_A)] = BIT_MASK(KEY_A);

	button_dev->open = button_open;
	button_dev->close = button_close;

	error = input_register_device(button_dev);
	if (error)
	{
		printk(KERN_ERR "button.c: Failed to register device\n");
		goto err_free_dev;
	}

	init_timer(&button_timer);
	button_timer.function = button_tiemer_function;
	add_timer(&button_timer);
	
	return 0;

 err_free_dev:
	input_free_device(button_dev);

	return error;
}

static void button_exit(void)
{
	input_unregister_device(button_dev);
	input_free_device(button_dev);
}

MODULE_LICENSE("GPL");
module_init(button_init);
module_exit(button_exit);

