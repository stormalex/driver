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
#include <linux/poll.h>

#define USB_MOUSE_MINOR_BASE    (200)
#define USB_MOUSE_MAX_DEV		(10)

#define USB_DEV_TYPE_NONE		0
#define USB_DEV_TYPE_MOUSE		1
#define USB_DEV_TYPE_KEYBOARD	2

#define RING_BUFFER_SIZE	(10)

struct usb_mouse_dev {
	char name[16];
	int devnum;
	unsigned int dev_type;				/* mouse or keyboard */

	/* interrupt endpoint info */
    struct urb* irq_urb;
    unsigned char int_in_endpointAddr;
	void* int_in_buffer;
	unsigned short int_in_size;
	unsigned int int_in_pipe;
	unsigned char int_in_interval;
	
	struct usb_device *usb_device;


	/* report descriptor info */
	void* report_descriptor;
	int report_desc_size;

	/* data buffer */
	void* ring_buf;
	void* wp;
	void* rp;
	int ring_data_size;

	/* wait queue */
	wait_queue_head_t read_q;
};

struct usb_mouse_map {
	unsigned long mouse_map [USB_MOUSE_MAX_DEV / (8*sizeof (unsigned long))];
};
static struct usb_mouse_map devmap;

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void usb_mouse_disconnect(struct usb_interface *intf);
static int usb_mouse_open(struct inode *inode, struct file *file);
static ssize_t usb_mouse_read(struct file *file, char __user *buf, size_t size, loff_t *offset);
static unsigned int usb_mouse_poll(struct file *, struct poll_table_struct *);
static int usb_mouse_release(struct inode *inode, struct file *file);

static const struct usb_device_id usb_mouse_id_table[] = {
    {USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE)},
    {},
};

static const struct file_operations usb_mouse_fops = {
    .owner = THIS_MODULE,
    .open = usb_mouse_open,
    .read = usb_mouse_read,
    .poll = usb_mouse_poll,
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

static int get_class_descriptor(struct usb_device *dev, int ifnum, unsigned char type, void *buf, int size)
{
	int result, retries = 4;
	memset(buf, 0, size);

	do{
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE | USB_DIR_IN, 
								(type << 8), ifnum, buf, size, USB_CTRL_GET_TIMEOUT);
		retries--;
	}while(result<size && retries);
	return result;
}

static int usb_mouse_display_info(struct usb_mouse_dev *usb_mouse_dev, struct usb_device *usb_device, struct usb_interface *intf)
{
    int i = 0;
    int j = 0;
	int alt_i = 0;
	int ret = 0;
	struct usb_descriptor_header *header;
	struct hid_descriptor *hid_desc;
	struct usb_device *dev = interface_to_usbdev(intf);
    
    printk("match a interface\n");
    printk("interface alt setting num=%d\n", intf->num_altsetting);
    printk("current interface setting bInterfaceNumber=%d\n", intf->cur_altsetting->desc.bInterfaceNumber);
    printk("current interface setting bAlternateSetting=%d\n", intf->cur_altsetting->desc.bAlternateSetting);
    
    printk("usb_device info:\n");
    printk("usb device devnum=%d\n", usb_device->devnum);
    
    printk("usb device speed:");
    switch(usb_device->speed) {
        case USB_SPEED_LOW:
            printk("LOW SPEED\n");
            break;
        case USB_SPEED_FULL:
            printk("FULL SPEED\n");
            break;
        case USB_SPEED_HIGH:
            printk("HIGH SPEED\n");
            break;
        case USB_SPEED_WIRELESS:
            printk("WIRELESS SPEED\n");
            break;
        case USB_SPEED_SUPER:
            printk("SUPER SPEED\n");
            break;
        default:
            printk("UNKNOWN SPEED\n");
            break;
    }
    
    printk("bus mA=%d\n", usb_device->bus_mA);
    printk("level=%d\n", usb_device->level);
    
    printk("descriptor info:\n");
    printk("  vendor id=0x%04x\n", le16_to_cpu(usb_device->descriptor.idVendor));
    printk("  product id=0x%04x\n", le16_to_cpu(usb_device->descriptor.idProduct));
    printk("  configure num=%d\n\n", usb_device->descriptor.bNumConfigurations);
    printk("  configure information:\n");
    
    for(i = 0; i < usb_device->descriptor.bNumConfigurations; i++) {
        printk("    configure index=%d\n", i);
        printk("    bConfigurationValue = %d\n", usb_device->config[i].desc.bConfigurationValue);
		printk("    configure string = %s\n", usb_device->config[i].string);
        printk("    bNumInterfaces = %d\n", usb_device->config[i].desc.bNumInterfaces);
		for(j = 0; j < usb_device->config[i].desc.bNumInterfaces; j++) {									/* 遍历所有接口 */
			printk("      configure %d,  interface index=%d\n", i, j);
			printk("      altsetting num=%d\n", usb_device->config[i].intf_cache[j]->num_altsetting);
			for(alt_i = 0; alt_i < usb_device->config[i].intf_cache[j]->num_altsetting; alt_i++) {					/* 遍历一个接口号下的所有设置，这里的每个描述符接口号一样，设置号不一样 */
				printk("        altsetting index=%d\n", alt_i);
				printk("        interface string = %s\n", usb_device->config[i].intf_cache[j]->altsetting[alt_i].string);
				printk("        alternate setting = %d\n", usb_device->config[i].intf_cache[j]->altsetting[alt_i].desc.bAlternateSetting);
				printk("        interface number = %d\n", usb_device->config[i].intf_cache[j]->altsetting[alt_i].desc.bInterfaceNumber);
				printk("        interface class = %d\n", usb_device->config[i].intf_cache[j]->altsetting[alt_i].desc.bInterfaceClass);
				printk("        interface protocal = %d\n", usb_device->config[i].intf_cache[j]->altsetting[alt_i].desc.bInterfaceProtocol);
				printk("        endpoint number = %d\n", usb_device->config[i].intf_cache[j]->altsetting[alt_i].desc.bNumEndpoints);

				if(usb_device->config[i].intf_cache[j]->altsetting[alt_i].extralen) {											/* 判断有没有额外描述符 */
					printk("          额外描述符:\n");
					header = (struct usb_descriptor_header *)usb_device->config[i].intf_cache[j]->altsetting[alt_i].extra;
					if(header->bDescriptorType == 0x21) {
						hid_desc = (struct hid_descriptor *)header;
						printk("            HID descriptor, len:%d\n", hid_desc->bLength);
						printk("            HID descriptor, type:0x%02x\n", hid_desc->bDescriptorType);
						printk("            HID bNumDescriptors, num:%d\n", hid_desc->bNumDescriptors);
						printk("            bDescriptorType : %s\n", hid_desc->desc[0].bDescriptorType==0x22?"report descriptor":"other descriptor");
						printk("            wDescriptorLength, num:%d\n", le16_to_cpu(hid_desc->desc[0].wDescriptorLength));
					}
				}
			}
		}
    }


	//set information
	if(intf->cur_altsetting->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD) {
		usb_mouse_dev->dev_type = USB_DEV_TYPE_KEYBOARD;
	}
	else if(intf->cur_altsetting->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE) {
		usb_mouse_dev->dev_type = USB_DEV_TYPE_MOUSE;
	}
	else {
		usb_mouse_dev->dev_type = USB_DEV_TYPE_NONE;
	}

	header = (struct usb_descriptor_header *)intf->cur_altsetting->extra;
	if((intf->cur_altsetting->extralen != 0) && (header->bDescriptorType == HID_DT_HID)) {
		hid_desc = (struct hid_descriptor *)header;
		if(hid_desc->bDescriptorType == HID_DT_REPORT) {
			usb_mouse_dev->report_desc_size = hid_desc->desc[0].wDescriptorLength;
			usb_mouse_dev->report_descriptor = kmalloc(usb_mouse_dev->report_desc_size, GFP_KERNEL);
			if(usb_mouse_dev->report_descriptor == NULL) {
				printk("kmalloc failed\n");
				return -ENOMEM;
			}
			ret = get_class_descriptor(dev, intf->cur_altsetting->desc.bInterfaceNumber, HID_DT_REPORT, usb_mouse_dev->report_descriptor, usb_mouse_dev->report_desc_size);
			if(ret != usb_mouse_dev->report_desc_size) {
				goto err1;
			}
		}
	}

	return 0;
err1:
	if(usb_mouse_dev->report_descriptor != NULL) {
		kfree(usb_mouse_dev->report_descriptor);
	}
	
    return -1;    
}

static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse_dev *usb_mouse_dev = (struct usb_mouse_dev*)urb->context;
    //char* buf = usb_mouse_dev->int_in_buffer;
    int status = urb->status;
    
    switch (status) {
	case -ENOENT:		/* synchronous unlink */
	case -ECONNRESET:	/* async unlink */
	case -ESHUTDOWN:	/* hardware going away */
		return;

	default:
		printk("urb status = %d\n", status);
        goto resubmit;

	case 0:			/* we got data:  port status changed */			/* urb被正确处理 */
		usb_mouse_dev->ring_data_size = 2;
        wake_up_interruptible(&usb_mouse_dev->read_q);
		
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

	file->private_data = (void *)usb_mouse_dev;

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
	}

	usb_mouse_dev->ring_buf = kmalloc(RING_BUFFER_SIZE * usb_mouse_dev->int_in_size, GFP_KERNEL);
	if(usb_mouse_dev->ring_buf == NULL) {
		printk("Ring buffer kmalloc failed\n");
		goto err2;
	}
	usb_mouse_dev->wp = usb_mouse_dev->ring_buf;
	usb_mouse_dev->rp = usb_mouse_dev->ring_buf;
	usb_mouse_dev->ring_data_size = 0;

	status = usb_submit_urb(usb_mouse_dev->irq_urb, GFP_NOIO);
	if (status < 0) {
        printk("usb_submit_urb error\n");
        goto err3;
    }
	
    return 0;
err3:
	if(usb_mouse_dev->ring_buf)
		kfree(usb_mouse_dev->ring_buf);
err2:
	if(usb_mouse_dev->int_in_buffer)
    	kfree(usb_mouse_dev->int_in_buffer);
err1:
	if(usb_mouse_dev->irq_urb)
    	usb_free_urb(usb_mouse_dev->irq_urb);
    return -1;
}

static ssize_t usb_mouse_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	unsigned long ret = 0;
	struct usb_mouse_dev *usb_mouse_dev = (struct usb_mouse_dev *)file->private_data;

	memset(usb_mouse_dev->ring_buf, 'A', 2);
	
	ret = copy_to_user(buf, usb_mouse_dev->ring_buf, 2);
	if(ret != 0) {
		printk("copy_to_user failed\n");
	}
	else {
		usb_mouse_dev->ring_data_size = 0;
	}
	
	return 2;
}

static unsigned int usb_mouse_poll(struct file *file, struct poll_table_struct *table)
{
	unsigned int mask = 0;
	struct usb_mouse_dev *usb_mouse_dev = (struct usb_mouse_dev *)file->private_data;

	poll_wait(file, &usb_mouse_dev->read_q, table);

	if(usb_mouse_dev->ring_data_size) {
		mask |= POLLIN | POLLRDNORM;							/* 可读取 */
	}
	return mask;
}

static int usb_mouse_release(struct inode *inode, struct file *file)
{
	struct usb_mouse_dev *usb_mouse_dev = (struct usb_mouse_dev *)file->private_data;

	if(usb_mouse_dev->ring_buf) {
		kfree(usb_mouse_dev->ring_buf);
		usb_mouse_dev->ring_buf = NULL;
	}
	if(usb_mouse_dev->int_in_buffer) {
		kfree(usb_mouse_dev->int_in_buffer);
		usb_mouse_dev->int_in_buffer = NULL;
	}
	usb_kill_urb(usb_mouse_dev->irq_urb);
	if(usb_mouse_dev->irq_urb) {
    	usb_free_urb(usb_mouse_dev->irq_urb);
		usb_mouse_dev->irq_urb = NULL;
	}

	file->private_data = NULL;
	
	return 0;
}

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_mouse_dev *usb_mouse_dev = NULL;
    struct usb_device *usb_device = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint_desc = NULL;
    int i = 0;

	usb_mouse_dev = kzalloc(sizeof(*usb_mouse_dev), GFP_KERNEL);
	if(usb_mouse_dev == NULL) {
		printk("kmalloc error\n");
		return -ENOMEM;
	}

	init_waitqueue_head(&usb_mouse_dev->read_q);

	usb_mouse_display_info(usb_mouse_dev, usb_device, intf);
	
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
	if(usb_mouse_dev->report_descriptor)
		kfree(usb_mouse_dev->report_descriptor);
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
