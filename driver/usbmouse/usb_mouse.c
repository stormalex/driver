#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>


static struct input_dev *usb_input_dev = NULL;
static char *usb_buf = NULL;
dma_addr_t usb_buf_phys;
struct urb *uk_urb = NULL;
int len;

static int usbmouse_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void usbmouse_disconnect(struct usb_interface *intf);


static struct usb_device_id usbmouse_id_table[] = {
		
		//支持的设备的厂家号
		{USB_DEVICE(0x62a, 0x7225)},  //鼠标
		{USB_DEVICE(0x930, 0x6544)},  //U盘
		//{USB_DEVICE_INTERFACE_PROTOCOL(0x930, 0x6544, 0x80)},
		//{USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE)},
		{},
};

static struct usb_driver usbmouse = {
		.name		= "usbmouse",
		.probe		= usbmouse_probe,
		.disconnect = usbmouse_disconnect,
		.id_table	= usbmouse_id_table,
};

void usbmouse_complete(struct urb *urb)
{
	int i;
	static int cnt = 0;
	printk("[%d]:", cnt++);
	for(i=0; i<len; i++)
	{
		printk("[%02x]:", usb_buf[i]);
	}
	printk("\n");

	//重新提交urb
	usb_submit_urb(uk_urb, GFP_KERNEL);
}


static int usbmouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int ret = 0;
	struct usb_device *usbmouse_device = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	int pipe;

	printk("idVendor=%d idProduct=%d\n", usbmouse_device->descriptor.idVendor, usbmouse_device->descriptor.idProduct);
	
	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;

	usb_input_dev = input_allocate_device();
	if(usb_input_dev == NULL)
	{
		printk("input_allocate_device() failed\n");
		return -1;
	}

	set_bit(EV_REL,usb_input_dev->evbit);		//能产生相对坐标事件
	set_bit(REL_X, usb_input_dev->relbit);		//能产生相对坐标中的X坐标事件
	set_bit(REL_Y, usb_input_dev->relbit);		//能产生相对坐标中的Y坐标事件

	ret = input_register_device(usb_input_dev);
	if(ret != 0)
	{
		printk("input_register_device() failed\n");
		goto err_register_input_device;
	}

	//数据源，USB设备的某个端点
	pipe = usb_rcvintpipe(usbmouse_device,endpoint->bEndpointAddress);

	//长度
	len = endpoint->wMaxPacketSize;

	//目的
	usb_buf = usb_alloc_coherent(usbmouse_device, len, GFP_ATOMIC, &usb_buf_phys);
	if(usb_buf == NULL)
	{
		printk("usb_alloc_urb() failed\n");
		ret = -ENOMEM;
		goto err_alloc_buffer;
	}
	

	//分配urb
	uk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(uk_urb == NULL)
	{
		printk("usb_alloc_urb() failed\n");
		ret = -ENOMEM;
		goto err_alloc_urb;
	}
	//设置urb
	usb_fill_int_urb(uk_urb, usbmouse_device, pipe, usb_buf, len, usbmouse_complete, NULL, endpoint->bInterval);
	uk_urb->transfer_dma = usb_buf_phys;
	uk_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(uk_urb, GFP_KERNEL);
	if(ret != 0)
	{
		printk("usb_submit_urb() failed\n");;
		goto err_submit_urb;
	}

	return ret;
	
err_submit_urb:
	usb_free_urb(uk_urb);
	
err_alloc_urb:
	usb_free_coherent(usbmouse_device, len, usb_buf, usb_buf_phys);
	
err_alloc_buffer:
	input_unregister_device(usb_input_dev);

err_register_input_device:
	input_free_device(usb_input_dev);
	
	return ret;
}

static void usbmouse_disconnect(struct usb_interface *intf)
{
	struct usb_device *usbmouse_device = interface_to_usbdev(intf);
	
	usb_kill_urb(uk_urb);
	usb_free_urb(uk_urb);
	usb_free_coherent(usbmouse_device, len, usb_buf, usb_buf_phys);
	input_unregister_device(usb_input_dev);
	input_free_device(usb_input_dev);
}

static int usbmouse_init(void)
{
	int ret = 0;
	printk("usbmouse_init()\n");
	
	ret = usb_register(&usbmouse);
	if(ret != 0)
	{
		printk("usb_register() failed\n");
		return -1;
	}
	
	return 0;
}

static void usbmouse_exit(void)
{
	printk("usbmouse_exit()\n");
	usb_deregister(&usbmouse);
}


module_init(usbmouse_init);
module_exit(usbmouse_exit);
MODULE_LICENSE("GPL");


