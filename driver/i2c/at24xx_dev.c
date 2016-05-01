#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/slab.h>


static struct i2c_board_info at24cxx_info = {
	I2C_BOARD_INFO("at24c02", 0x50),
};

static struct i2c_client *at24c02_client = NULL;

static int at24cxx_dev_init(void)
{
	struct i2c_adapter *adapter = NULL;

	adapter = i2c_get_adapter(0);
	
	/* register i2c_device */
	at24c02_client = i2c_new_device(adapter, &at24cxx_info);

	i2c_put_adapter(adapter);
	
	return 0;
}

/* del i2c_device */
static void at24cxx_dev_exit(void)
{
	i2c_unregister_device(at24c02_client);
}


MODULE_LICENSE("GPL");
module_init(at24cxx_dev_init);
module_exit(at24cxx_dev_exit);

