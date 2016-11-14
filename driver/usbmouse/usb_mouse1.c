/*****************************************************
 *
 * 将设备作为一个普通的USB设备注册进usb系统，主设备号是180
 *
 *****************************************************/

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hid.h>


#define USB_MOUSE_MINOR_BASE    (200)

static struct urb *usb_mouse_urb = NULL;

static const struct usb_device_id usb_mouse_id_table[] = {
    {USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE)},
};

static void usb_mouse_display_info(struct usb_device *usb_device, struct usb_interface *intf)
{
    int i = 0;
    
    printk("match a interface\n");
    printk("interface alt setting num=%d\n", intf->num_altsetting);
    printk("current interface setting bInterfaceNumber=%d\n", intf->cur_altsetting->desc.bInterfaceNumber);
    printk("current interface setting bAlternateSetting=%d\n", intf->cur_altsetting->desc.bAlternateSetting);
    
    printk("usb_device info:\n");
    printk("usb device devnum=%d\n", usb_device->devnum);
    
    printk("usb device speed:\n");
    switch(usb_device->speed) {
        case USB_SPEED_LOW:
            printk("\tLOW SPEED\n");
            break;
        case USB_SPEED_FULL:
            printk("\tFULL SPEED\n");
            break;
        case USB_SPEED_HIGH:
            printk("\tHIGH SPEED\n");
            break;
        case USB_SPEED_WIRELESS:
            printk("\tWIRELESS SPEED\n");
            break;
        case USB_SPEED_SUPER:
            printk("\tSUPER SPEED\n");
            break;
        default:
            printk("\tUNKNOWN SPEED\n");
            break;
    }
    
    printk("bus mA=%d\n", usb_device->bus_mA);
    printk("level=%d\n", usb_device->level);
    
    printk("descriptor info:\n");
    printk("\tvendor id=0x%04x\n", le16_to_cpu(usb_device->descriptor.idVendor));
    printk("\tproduct id=0x%04x\n", le16_to_cpu(usb_device->descriptor.idProduct));
    printk("\tconfigure num=%d\n", usb_device->descriptor.bNumConfigurations);
    printk("\n\tconfigure:\n");
    
    for(i = 0; i < usb_device->descriptor.bNumConfigurations; i++) {
        printk("\t\ti=%d\n", i);
        printk("\t\tbConfigurationValue = %d\n", usb_device->config[i].desc.bConfigurationValue);
        printk("\t\tbNumInterfaces = %d\n", usb_device->config[i].desc.bNumInterfaces);
    }
    return;    
}

static int usb_mouse_open(struct inode *inode, struct file *file)
{
    printk("open the usb mouse, major=%d, minor=%d\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
    return 0;
}

static const struct file_operations adu_fops = {
    .owner = THIS_MODULE,
    .open = usb_mouse_open,
};

static struct usb_class_driver usb_mouse_class = {
	.name = "usb_mouse_class",
	.fops = &adu_fops,
	.minor_base = USB_MOUSE_MINOR_BASE,
};

static void usb_mouse_irq(struct urb *urb)
{
    char* buf = (char*)urb->context;
    int status = urb->status;
    int i;
    
    switch (status) {
	case -ENOENT:		/* synchronous unlink */
	case -ECONNRESET:	/* async unlink */
	case -ESHUTDOWN:	/* hardware going away */
		return;

	default:
		printk("urb status = %d\n", status);
        goto resubmit;

	case 0:			/* we got data:  port status changed */			/* 说明urb被顺利处理了 */
        printk("actual data size = %d\n", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i)					/* actual_length是实际的字节数，因为每个hub的端口数都是不一样的，这个bitmap里最大的容量是端口数+1，额外的一位是为整个hub提供的 */
			printk("0x%02x\n", buf[i]);
        printk("\n");
		break;
	}
    
resubmit:
    if ((status = usb_submit_urb (usb_mouse_urb, GFP_ATOMIC)) != 0
			&& status != -ENODEV && status != -EPERM)
        printk("resubmit --> %d\n", status);
}

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *usb_device = interface_to_usbdev(intf);
    struct usb_host_endpoint *endpoint = intf->cur_altsetting->endpoint;
    unsigned int pipe;
    int maxp;
    void *buf = NULL;
    int status;
    
    usb_mouse_display_info(usb_device, intf);
    
    if(usb_register_dev(intf, &usb_mouse_class) < 0) {
        printk("usb_register_dev error\n");
        return -1;
    }
    
    usb_mouse_urb = usb_alloc_urb(0, GFP_KERNEL);
    if(usb_mouse_urb == NULL) {
        printk("usb_alloc_urb error\n");
    }
    
    pipe = usb_rcvintpipe(usb_device, endpoint->desc.bEndpointAddress);
    maxp = usb_maxpacket(usb_device, pipe, usb_pipeout(pipe));
    buf = kmalloc(maxp, GFP_KERNEL);
    if(buf == NULL) {
        printk("kmalloc error\n");
        goto err1;
    }
    printk("endpoint max data size = %d\n", maxp);
    usb_fill_int_urb(usb_mouse_urb, usb_device, pipe, buf, maxp, usb_mouse_irq, buf, endpoint->desc.bInterval);
    
    status = usb_submit_urb(usb_mouse_urb, GFP_NOIO);
	if (status < 0) {
        printk("usb_submit_urb error\n");
        goto err1;
    }
    
    return 0;

err1:
    usb_free_urb(usb_mouse_urb);
    usb_deregister_dev(intf, &usb_mouse_class);
    return -1;
}

static void usb_mouse_disconnect(struct usb_interface *intf)
{
    usb_free_urb(usb_mouse_urb);
    usb_deregister_dev(intf, &usb_mouse_class);
}

static struct usb_driver usb_mouse_driver = {
    .name = "usb_mouse1",
    .probe = usb_mouse_probe,
    .disconnect = usb_mouse_disconnect,
    .id_table = usb_mouse_id_table,
};

static int __init usb_mouse_init(void)
{
	if(usb_register(&usb_mouse_driver) < 0) {
        printk("usb_register usb_mouse_driver failed\n");
        return -1;
    }
	return 0;
}

static void __exit usb_mouse_exit(void)
{
	usb_deregister(&usb_mouse_driver);
    return;
}

module_init(usb_mouse_init);
module_exit(usb_mouse_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB mouse1");
