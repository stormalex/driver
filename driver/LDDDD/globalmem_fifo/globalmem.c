#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

#define GLOBALMEM_SIZE 	0x1000
#define GLOBALMEM_MAJOR	230
#define DEVICE_NUM		10

#define GLOBALMEM_MAGIC 'g'
#define MEM_CLEAR _IO(GLOBALMEM_MAGIC, 0)

struct globalmem_dev{
	struct cdev cdev;
	unsigned int current_len;
	unsigned char mem[GLOBALMEM_SIZE];
	struct mutex mutex;
	wait_queue_head_t r_wait;		//读等待队列
	wait_queue_head_t w_wait;		//写等待队列
};

static int globalmem_major = GLOBALMEM_MAJOR;
module_param(globalmem_major, int, S_IRUGO);
struct globalmem_dev *globalmem_devp;

static int globalmem_open(struct inode *inode, struct file* filp);
static ssize_t globalmem_read(struct file* filp, char __user* buf, size_t size, loff_t* ppos);
static ssize_t globalmem_write(struct file *filp, const char __user* buf, size_t size, loff_t* ppos);
static loff_t globalmem_llseek(struct file *filp, loff_t offset, int orig);
static long globalmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int globalmem_release(struct inode* inode, struct file* filp);

static const struct file_operations globalmem_fops = {
	.owner	=	THIS_MODULE,
	.llseek	=	globalmem_llseek,
	.read	=	globalmem_read,
	.write	=	globalmem_write,
	.unlocked_ioctl	=	globalmem_ioctl,
	.open	=	globalmem_open,
	.release	=	globalmem_release,
};

static int globalmem_open(struct inode *inode, struct file* filp)
{
	struct globalmem_dev *dev = container_of(inode->i_cdev, struct globalmem_dev, cdev);
	filp->private_data = dev;
	return 0;
}

static int globalmem_release(struct inode* inode, struct file* filp)
{
	return 0;
}

static ssize_t globalmem_read(struct file* filp, char __user* buf, size_t size, loff_t* ppos)
{
	unsigned int count = size;
	int ret = 0;
	struct globalmem_dev* dev = filp->private_data;
	DECLARE_WAITQUEUE(wait, current);					//定义 等待队列元素

	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->r_wait, &wait);				//将 等待队列元素 加入 读等待队列
	
	while(dev->current_len == 0)
	{
		if(filp->f_flags & O_NONBLOCK)
		{
			ret = EAGAIN;
			goto out;
		}
		
		__set_current_state(TASK_INTERRUPTIBLE);		//将 进程标记为 TASK_INTERRUPTIBLE, 进程并未真正睡眠
		mutex_unlock(&dev->mutex);
		
		schedule();										//执行进程调度，进程进入睡眠
		if(signal_pending(current))						//进程被唤醒后，判断是否是由信号唤醒的
		{
			ret = -ERESTARTSYS;
			goto out2;
		}
		
		mutex_lock(&dev->mutex);
	}
	
	if(count > dev->current_len)
		count = dev->current_len;
	
	if(copy_to_user(buf, dev->mem, count))
	{
		ret = -EFAULT;
		goto out;
	}
	else
	{
		memcpy(dev->mem, dev->mem + count, dev->current_len - count);
		dev->current_len -= count;
		printk("read %d byte(s), current_len:%d\n", count, dev->current_len);
		
		wake_up_interruptible(&dev->w_wait);				//唤醒写等待队列上的进程
		
		ret = count;
	}

out:
	mutex_unlock(&dev->mutex);
out2:
	remove_wait_queue(&dev->r_wait, &wait);		//将 等待队列元素 从 读等待队列 移除
	set_current_state(TASK_RUNNING);			//将 进程标记为 TASK_RUNNING
	return ret;
}

static ssize_t globalmem_write(struct file *filp, const char __user* buf, size_t size, loff_t* ppos)
{
	unsigned int count = size;
	int ret = 0;
	struct globalmem_dev* dev = filp->private_data;
	DECLARE_WAITQUEUE(wait, current);
	
	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->w_wait, &wait);
	
	while(dev->current_len == GLOBALMEM_SIZE)
	{
		if(filp->f_flags & O_NONBLOCK)
		{
			ret = -EAGAIN;
			goto out;
		}
		
		__set_current_state(TASK_INTERRUPTIBLE);
		
		mutex_unlock(&dev->mutex);
		
		schedule();
		if(signal_pending(current))
		{
			ret = -ERESTARTSYS;
			goto out2;
		}
		
		mutex_lock(&dev->mutex);
	}
	
	if(count > GLOBALMEM_SIZE - dev->current_len)
		count = GLOBALMEM_SIZE - dev->current_len;
	
	if(copy_from_user(dev->mem + dev->current_len, buf, count))
	{
		ret = -EFAULT;
		goto out;
	}
	else
	{
		dev->current_len += count;
		printk("written %d bytes(s), current_len:%d\n", count, dev->current_len);
		
		wake_up_interruptible(&dev->r_wait);				//唤醒读等待队列上的进程
		
		ret = count;
	}
	
out:
	mutex_unlock(&dev->mutex);
out2:
	remove_wait_queue(&dev->w_wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static loff_t globalmem_llseek(struct file *filp, loff_t offset, int orig)
{
	loff_t ret = 0;
	switch(orig) {
		case 0:
			if(offset < 0) {
				ret = -EINVAL;
				break;
			}
			if((unsigned int)offset > GLOBALMEM_SIZE) {
				ret = -EINVAL;
				break;
			}
			filp->f_pos = (unsigned int)offset;
			ret = filp->f_pos;
			break;
		case 1:
			if((filp->f_pos + offset) > GLOBALMEM_SIZE) {
				ret = -EINVAL;
				break;
			}
			if((filp->f_pos + offset) < 0) {
				ret = -EINVAL;
				break;
			}
		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

static long globalmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct globalmem_dev *dev = filp->private_data;
	
	switch(cmd) {
		case MEM_CLEAR:
			mutex_lock(&dev->mutex);
			memset(dev->mem, 0, GLOBALMEM_SIZE);
			mutex_unlock(&dev->mutex);
			printk("globalmem is set to zero\n");
			break;
			
		default:
			return -EINVAL;
	}
	
	return 0;
}

static void globalmem_setup_cdev(struct globalmem_dev* dev, int index)
{
	int err, devno = MKDEV(globalmem_major, index);
	
	cdev_init(&dev->cdev, &globalmem_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if(err)
		printk("Error %d adding globalmem %d\n", err, index);
}

static int __init globalmem_init(void)
{
	int ret;
	int i;
	dev_t devno = MKDEV(globalmem_major, 0);
	
	if(globalmem_major)
	{
		ret = register_chrdev_region(devno, DEVICE_NUM, "globalmem");
	}
	else
	{
		ret = alloc_chrdev_region(&devno, 0, DEVICE_NUM, "globalmem");
		globalmem_major = MAJOR(devno);
	}
	if(ret < 0)
		return ret;
	
	globalmem_devp = kzalloc(sizeof(struct globalmem_dev) * DEVICE_NUM, GFP_KERNEL);
	if(!globalmem_devp)
	{
		ret = -ENOMEM;
		goto fail_malloc;
	}
	
	for(i = 0; i < DEVICE_NUM; i++)
	{
		mutex_init(&(globalmem_devp[i].mutex));
		init_waitqueue_head(&(globalmem_devp[i].r_wait));
		init_waitqueue_head(&(globalmem_devp[i].w_wait));
	}
	
	for(i = 0; i < DEVICE_NUM; i++)
		globalmem_setup_cdev(globalmem_devp + i, i);
	return 0;

fail_malloc:
	unregister_chrdev_region(devno, 1);
	return ret;
}

static void __exit globalmem_exit(void)
{
	int i;
	
	for(i = 0; i < DEVICE_NUM; i++)
		cdev_del(&(globalmem_devp + i)->cdev);
	kfree(globalmem_devp);
	unregister_chrdev_region(MKDEV(globalmem_major, 0), DEVICE_NUM);
}

module_init(globalmem_init);
module_exit(globalmem_exit);
MODULE_LICENSE("GPL");