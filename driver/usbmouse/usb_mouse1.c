/*****************************************************
 *
 * 注册主设备号为 180 的设备节点，一套驱动可以支持多个设备的鼠标驱动
 *
 *****************************************************/

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hid.h>


#define USB_MOUSE_MINOR_BASE    (200)
#define USB_MOUSE_MAX_DEV		(10)

struct usb_mouse_dev {
	char name[16];
	int devnum;
    struct urb* irq_urb;
    unsigned char int_in_endpointAddr;
	void* int_in_buffer;
	unsigned short int_in_size;
	unsigned int int_in_pipe;
	unsigned char int_in_interval;
	struct usb_device *usb_device;
};

struct usb_mouse_map {
	unsigned long mouse_map [USB_MOUSE_MAX_DEV / (8*sizeof (unsigned long))];
};
static struct usb_mouse_map devmap;

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void usb_mouse_disconnect(struct usb_interface *intf);
static int usb_mouse_open(struct inode *inode, struct file *file);
static ssize_t usb_mouse_read(struct file *file, char __user *buf, size_t size, loff_t *offset);
static int usb_mouse_release(struct inode *inode, struct file *file);

static const struct usb_device_id usb_mouse_id_table[] = {
    {USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE)},
    {},
};

static const struct file_operations usb_mouse_fops = {
    .owner = THIS_MODULE,
    .open = usb_mouse_open,
    .read = usb_mouse_read,
    .release = usb_mouse_release,
};

static struct usb_class_driver usb_mouse_class = {
	//.name = "usb_mouse_class",
	.fops = &usb_mouse_fops,
	.minor_base = USB_MOUSE_MINOR_BASE,
};


static struct usb_driver usb_mouse_driver = {
    .name = "usb_mouse1",
    .probe = usb_mouse_probe,
    .disconnect = usb_mouse_disconnect,
    .id_table = usb_mouse_id_table,
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

static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse_dev *usb_mouse_dev = (struct usb_mouse_dev*)urb->context;
    char* buf = usb_mouse_dev->int_in_buffer;
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

	case 0:			/* we got data:  port status changed */			/* urb被正确处理 */
        printk("actual data size = %d\n", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i)
			printk("0x%02x\n", buf[i]);
        printk("\n");
		break;
	}
    
resubmit:
    if ((status = usb_submit_urb (urb, GFP_ATOMIC)) != 0
			&& status != -ENODEV && status != -EPERM)
        printk("resubmit --> %d\n", status);
}

static int usb_mouse_open(struct inode *inode, struct file *file)
{
    int status;
    unsigned int pipe;
	struct usb_mouse_dev *usb_mouse_dev = NULL;
	struct usb_interface *intf = NULL;
	struct usb_device *usb_device = NULL;
    
    printk("open the usb mouse, major=%d, minor=%d\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev));

	intf = usb_find_interface(&usb_mouse_driver, MINOR(inode->i_rdev));
	usb_mouse_dev = usb_get_intfdata(intf);
	printk("usb_mouse_dev = %p\n", usb_mouse_dev);

	usb_device = usb_mouse_dev->usb_device;
	pipe = usb_mouse_dev->int_in_pipe;
	
    usb_mouse_dev->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
    if(usb_mouse_dev->irq_urb == NULL) {
        printk("usb_alloc_urb error\n");
        return -1;
    }

	if(usb_mouse_dev->int_in_endpointAddr && usb_mouse_dev->int_in_buffer == NULL) {
		usb_mouse_dev->int_in_buffer = kmalloc(usb_mouse_dev->int_in_size, GFP_KERNEL);
		if(usb_mouse_dev->int_in_buffer == NULL) {
			printk("kmalloc buffer failed\n");
			goto err1;
		}
		usb_fill_int_urb(usb_mouse_dev->irq_urb, usb_device, pipe, usb_mouse_dev->int_in_buffer, usb_mouse_dev->int_in_size, usb_mouse_irq, usb_mouse_dev, usb_mouse_dev->int_in_interval);
    
	    status = usb_submit_urb(usb_mouse_dev->irq_urb, GFP_NOIO);
		if (status < 0) {
	        printk("usb_submit_urb error\n");
	        goto err2;
	    }
	}
    return 0;
err2:
    kfree(usb_mouse_dev->int_in_buffer);
err1:
    usb_free_urb(usb_mouse_dev->irq_urb);
    return -1;
}

static ssize_t usb_mouse_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	return 0;
}

static int usb_mouse_release(struct inode *inode, struct file *file)
{
	struct usb_interface *intf = NULL;
	struct usb_mouse_dev *usb_mouse_dev = NULL;
	intf = usb_find_interface(&usb_mouse_driver, MINOR(inode->i_rdev));
	usb_mouse_dev = usb_get_intfdata(intf);
	if(usb_mouse_dev->int_in_buffer) {
		kfree(usb_mouse_dev->int_in_buffer);
		usb_mouse_dev->int_in_buffer = NULL;
	}
	usb_kill_urb(usb_mouse_dev->irq_urb);
    usb_free_urb(usb_mouse_dev->irq_urb);
	return 0;
}

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_mouse_dev *usb_mouse_dev = NULL;
    struct usb_device *usb_device = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint_desc = NULL;
    int i = 0;
	
    usb_mouse_display_info(usb_device, intf);

	usb_mouse_dev = kzalloc(sizeof(*usb_mouse_dev), GFP_KERNEL);
	if(usb_mouse_dev == NULL) {
		printk("kmalloc error\n");
		return -ENOMEM;
	}
	for(i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		printk("check %d endpoint\n", i+1);
		endpoint_desc = &intf->cur_altsetting->endpoint[i].desc;
		/* 如果中断输入端点还没有设置， 检测这个端点是不是输入类型， 检测这个端点是不是中断类型 */
		if(!usb_mouse_dev->int_in_endpointAddr && (endpoint_desc->bEndpointAddress & USB_DIR_IN) && ((endpoint_desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)) {
			usb_mouse_dev->int_in_endpointAddr = endpoint_desc->bEndpointAddress;
			usb_mouse_dev->int_in_interval = endpoint_desc->bInterval;
			usb_mouse_dev->int_in_pipe = usb_rcvintpipe(usb_device, endpoint_desc->bEndpointAddress);
			usb_mouse_dev->int_in_size = __le16_to_cpu(endpoint_desc->wMaxPacketSize);
		}
	}
	usb_mouse_dev->usb_device = usb_device;

	
	usb_set_intfdata(intf, usb_mouse_dev);
	usb_mouse_dev->devnum = find_next_zero_bit(devmap.mouse_map, USB_MOUSE_MAX_DEV, 1);
	if (usb_mouse_dev->devnum >= USB_MOUSE_MAX_DEV) {
		printk ("too many usb mouse device\n");
		goto err1;
	}
	set_bit (usb_mouse_dev->devnum, devmap.mouse_map);
	
	usb_mouse_class.name = usb_mouse_dev->name;
	sprintf(usb_mouse_class.name, "usb_mouse_%d", usb_mouse_dev->devnum);
	
    if(usb_register_dev(intf, &usb_mouse_class) < 0) {
        printk("usb_register_dev error\n");
        goto err1;
    }
	printk("register a usb device successfull: usb_mouse_dev=%p\n", usb_mouse_dev);
    return 0;

err1:
	kfree(usb_mouse_dev);

    return -1;
}

static void usb_mouse_disconnect(struct usb_interface *intf)
{
	struct usb_mouse_dev *usb_mouse_dev = NULL;

	usb_mouse_dev = usb_get_intfdata(intf);
	
    usb_deregister_dev(intf, &usb_mouse_class);
	usb_set_intfdata(intf, NULL);
	if(usb_mouse_dev->int_in_buffer)
		kfree(usb_mouse_dev->int_in_buffer);
	kfree(usb_mouse_dev);
	clear_bit(usb_mouse_dev->devnum, devmap.mouse_map);
}

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
