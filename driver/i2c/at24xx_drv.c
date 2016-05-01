#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/fs.h>


static int major;
static struct class *class;
static struct i2c_client *at24cxx_client;

/* input  	buf[0]: addr
 *  output 	buf[0]: data
 */
static ssize_t at24cxx_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
	u8 addr, data;

	copy_from_user(&addr, buf, 1);

	data = i2c_smbus_read_byte_data(at24cxx_client, addr);

	copy_to_user(buf, &data, 1);

	return 1;
}

/* buf[0]: addr
 *  buf[1]: write data
 */
static ssize_t at24cxx_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
	u8 w_buf[2];
	u8 addr, data;
	s32 ret = 0;

	copy_from_user(w_buf, buf, 2);
	addr = w_buf[0];
	data = w_buf[1];

	ret = i2c_smbus_write_byte_data(at24cxx_client, addr, data);
	if(ret != 0)
		return -EIO;

	return 2;
}


static const struct file_operations at24cxx_fops={
	.owner = THIS_MODULE,
	.read  = at24cxx_read,
	.write = at24cxx_write,
};
static int __devinit at24cxx_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	printk("at24cxx probe...\n");
	at24cxx_client = client;
	
	major = register_chrdev(0, "at24cxx", &at24cxx_fops);

	class = class_create(THIS_MODULE,"at24cxx");
	device_create(class, NULL, MKDEV(major, 0), NULL, "at24c02");
	

	return 0;
}

static int __devexit at24cxx_remove(struct i2c_client *client)
{
	printk("at24cxx remove...\n");
	device_destroy(class, MKDEV(major, 0));
	class_destroy(class);
	unregister_chrdev(major, "at24cxx");
	
	return 0;
}


/* create and set i2c_driver */
static const struct i2c_device_id at24cxx_id_table[] = {
	{ "at24c02", 0 },
	{ }
};

static struct i2c_driver at24cxx_driver = {
	.driver = {
		.name	= "s3c2440",
		.owner	= THIS_MODULE,
	},
	.probe		= at24cxx_probe,
	.remove		= at24cxx_remove,
	.id_table	= at24cxx_id_table,
};


static int at24cxx_drv_init(void)
{
	int ret = 0;
	
	/* register i2c_driver */
	ret = i2c_add_driver(&at24cxx_driver);
	
	return 0;
}

/* del i2c_driver */
static void at24cxx_drv_exit(void)
{
	i2c_del_driver(&at24cxx_driver);
}


MODULE_LICENSE("GPL");
module_init(at24cxx_drv_init);
module_exit(at24cxx_drv_exit);

