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
#include <linux/cdev.h>
#include <linux/i2c.h>

static int major;
static struct class *class = NULL;
static struct i2c_client *at24cxx_client;

//板子上的i2c设备7bit地址是0101 0000
static const unsigned short at24_addr_list[] = { 0x60, 0x50, 0x51, I2C_CLIENT_END };

static const struct i2c_device_id at24_id[] = {
	{"test_dev_1", 0},
	{"test_dev_2", 0},
	{"test_dev_3", 0},
	{"test_dev_4", 0},
	{"test_dev_5", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, at24_id);

static int at24_detect(struct i2c_client* client, struct i2c_board_info* info)
{
	//能运行到这个函数说明这个设备的地址是正确的，实现这个函数中需要返回-ENODEV或者填写info->type
	
	//struct i2c_adapter* adapter = client->adapter;
	printk("[i2c_driver]detect device:%s 0x%02x\n", client->name, client->addr);
	/*if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		printk("[i2c_driver]at24_test_driver: detect failed, I2C_FUNC_I2C byte data not supported!\n");
		return -ENODEV;
	} else {
		printk("[i2c_driver]at24_test_driver: detect ok, I2C_FUNC_I2C supported\n");
	}*/

	strlcpy(info->type, "test_dev_1", I2C_NAME_SIZE);  //这里的名字需要和 at24_id 表里的名字匹配到，驱动才能匹配到设备！！
	return 0;
}

static ssize_t at24cxx_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
	u8 addr, data;

	copy_from_user(&addr, buf, 1);

	data = i2c_smbus_read_byte_data(at24cxx_client, addr);

	copy_to_user(buf, &data, 1);

	return 1;
}

static ssize_t at24cxx_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
	u8 w_buf[2];
	u8 addr,data;
	s32 ret = 0;
	
	copy_from_user(w_buf, buf, 2);
	
	addr = w_buf[0];
	data = w_buf[1];
	
	ret = i2c_smbus_write_byte_data(at24cxx_client, addr, data);
	if(ret != 0)
		return -EIO;

	return count;
}

static const struct file_operations at24cxx_fops={
	.owner = THIS_MODULE,
	.read  = at24cxx_read,
	.write = at24cxx_write,
};

static int at24_probe(struct i2c_client* client, const struct i2c_device_id* id)
{
	struct device* dev = NULL;
	printk("match device:%s 0x%02x\n", client->name, client->addr);
	
	at24cxx_client = client;
	
	major = register_chrdev(0, "at24xx", &at24cxx_fops);
	
	class = class_create(THIS_MODULE, "at24xx");
	if(!class) {
		printk("class_create failed\n");
	}
	dev = device_create(class, NULL, MKDEV(major, 0), NULL, "at24c02");
	if(!dev) {
		printk("device_create failed\n");
	}
	
	return 0;
}

static int __devexit at24_remove(struct i2c_client *client)
{
	printk("remove device:%s 0x%02x\n", client->name, client->addr);
	return 0;
}

static struct i2c_driver at24c08_driver = {
	.class  = I2C_CLASS_HWMON,
	.driver = {
		.name = "at24_test_driver",
		.owner = THIS_MODULE,
	},
	/*当实现 detech 这个成员函数时，需要有address_list这个地址表，i2c-core会自动扫描地址表的地址，然后创建i2c_client，
	* 不需要其他模块添加设备或手动添加设备，甚至连id_table都不用实现。这种方法最智能，最灵活，可能实际中会过于灵活
	* 具体参见 i2c-core.c 文件中的 i2c_detect() 函数
	*/
	.detect = at24_detect,
	.probe = at24_probe,
	.remove = __devexit_p(at24_remove),
	.id_table = at24_id,
	.address_list = at24_addr_list,
};

static int at24_init(void)
{
	return i2c_add_driver(&at24c08_driver);
}

static void at24_exit(void)
{
	i2c_del_driver(&at24c08_driver);
}

module_init(at24_init);
module_exit(at24_exit);
MODULE_LICENSE("GPL");