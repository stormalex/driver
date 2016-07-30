#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of_i2c.h>
#include <linux/of_gpio.h>

#include <asm/irq.h>

#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <plat/regs-iic.h>
#include <plat/iic.h>

#define DEBUG 1

enum s3c2440_i2c_state {
	STATE_IDLE = 0,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct s3c2440_i2c_regs{
	unsigned int iiccon;
	unsigned int iicstat;
	unsigned int iicadd;
	unsigned int iicds;
	unsigned int iiclc;
};

static struct s3c2440_i2c_regs *s3c2440_i2c_regs;



struct s3c2440_i2c_xfer_data{
	struct i2c_msg *msgs;
	int msg_num;
	int cur_msg;
	int cur_ptr;
	//enum s3c2440_i2c_state state;
	int state;	
	int err;
	wait_queue_head_t wait;
};

struct s3c2440_i2c_xfer_data s3c2440_i2c_xfer_data;


static void s3c2440_i2c_start(void)
{
	s3c2440_i2c_xfer_data.state = STATE_START;
	
	if(s3c2440_i2c_xfer_data.msgs->flags & I2C_M_RD)		//read
	{
		s3c2440_i2c_regs->iicds = s3c2440_i2c_xfer_data.msgs->addr << 1;
		s3c2440_i2c_regs->iicstat = 0xb0;
	}
	else		//write
	{
		s3c2440_i2c_regs->iicds = s3c2440_i2c_xfer_data.msgs->addr << 1;
		s3c2440_i2c_regs->iicstat = 0xf0;
	}
}

static void s3c2440_i2c_stop(int err)
{
	s3c2440_i2c_xfer_data.state = STATE_STOP;
	s3c2440_i2c_xfer_data.err = err;

	if(s3c2440_i2c_xfer_data.msgs->flags & I2C_M_RD)		//read
	{
		s3c2440_i2c_regs->iicstat = 0x90;
		s3c2440_i2c_regs->iiccon = 0xaf;
		ndelay(50);
	}
	else		//write
	{
		s3c2440_i2c_regs->iicstat = 0xd0;
		s3c2440_i2c_regs->iiccon = 0xaf;
		ndelay(50);
	}

	//唤醒
	wake_up(&s3c2440_i2c_xfer_data.wait);
}

static int s3c2440_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct i2c_msg *msgs_p = msgs;
	int i,j;
	printk("num=%d\n", num);
	for(i=0; i<num; i++)
	{
		printk("\naddr=0x%x\n", msgs_p->addr);
		printk("flags=0x%x\n", msgs_p->flags);
		printk("msg len=%d\n\n", msgs_p->len);
		for(j=0; j<msgs_p->len; j++)
		{
			printk("\t0x%x\n", msgs_p->buf[j]);
		}
		msgs_p++;
	}

	unsigned long timeout;
	
	s3c2440_i2c_xfer_data.msgs = msgs;
	s3c2440_i2c_xfer_data.msg_num = num;
	s3c2440_i2c_xfer_data.cur_msg = 0;
	s3c2440_i2c_xfer_data.cur_ptr = 0;
	s3c2440_i2c_xfer_data.err = -ENODEV;

	s3c2440_i2c_start();

	//休眠
	timeout = wait_event_timeout(s3c2440_i2c_xfer_data.wait, s3c2440_i2c_xfer_data.state == STATE_STOP, HZ * 5);
	if(0 == timeout)
	{
		printk("s3c2440_i2c_xfer time out\n");
		return -ETIMEDOUT;
	}
	else
	{
		return s3c2440_i2c_xfer_data.err;
	}
}


static u32 s3c2440_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}


static const struct i2c_algorithm s3c2440_i2c_algo = {
		.master_xfer   = s3c2440_i2c_xfer,
		.functionality = s3c2440_i2c_func,
};

/* 分配、设置i2c_adapter */
static struct i2c_adapter s3c2440_i2c_adapter={
		.name = "s3c2440_i2c_adapter",
		.algo = &s3c2440_i2c_algo,
		.owner= THIS_MODULE,
};

static int isLastMsg(void)
{
	return (s3c2440_i2c_xfer_data.cur_msg == s3c2440_i2c_xfer_data.msg_num - 1);
}

static int isEndData(void)
{
	return (s3c2440_i2c_xfer_data.cur_ptr >= s3c2440_i2c_xfer_data.msgs->len);
}

static int isLastData(void)
{
	return (s3c2440_i2c_xfer_data.cur_ptr == s3c2440_i2c_xfer_data.msgs->len - 1);
}

static irqreturn_t s3c2440_i2c_xfer_irq(int irq, void *dev_id)
{
	 unsigned int iicst;
	 
	 iicst = s3c2440_i2c_regs->iicstat;
	 if(iicst & 0x08){printk("s3c2440 i2c Bus arbitration failed\n\r");}

	 switch(s3c2440_i2c_xfer_data.state)
	 {
		case STATE_START:		//发出START和设备地址后产生中断
		{	
			//如果没有ACK，返回错误
			if (iicst & S3C2410_IICSTAT_LASTBIT)
			{
				s3c2440_i2c_stop(-ENODEV);
				break;
			}
			if(isLastMsg() && isEndData())
			{
				s3c2440_i2c_stop(0);
				break;
			}

			//进入下一个状态
			if(s3c2440_i2c_xfer_data.msgs->flags & I2C_M_RD)		//read
			{
				s3c2440_i2c_xfer_data.state = STATE_READ;
				goto next_read;
			}
			else
			{
				s3c2440_i2c_xfer_data.state = STATE_WRITE;
			}
		}
		case STATE_WRITE:
		{	
			//如果没有ACK，返回错误
			if (iicst & S3C2410_IICSTAT_LASTBIT)
			{
				s3c2440_i2c_stop(-ENODEV);
				break;
			}
			if(!isEndData())	//如果当前msg还有数据要发送
			{
				s3c2440_i2c_regs->iicds = s3c2440_i2c_xfer_data.msgs->buf[s3c2440_i2c_xfer_data.cur_ptr];
				s3c2440_i2c_xfer_data.cur_ptr++;
				ndelay(50);
				s3c2440_i2c_regs->iiccon = 0xaf;		//恢复I2C传输
				break;
			}
			else if(!isLastMsg())
			{
				//开始处理下一个消息
				s3c2440_i2c_xfer_data.msgs++;
				s3c2440_i2c_xfer_data.cur_msg++;
				s3c2440_i2c_xfer_data.cur_ptr=0;
				s3c2440_i2c_xfer_data.state = STATE_START;

				//发出START信号和发出设备地址
				s3c2440_i2c_start();
				break;
			}
			else
			{
				//如果是最后一个消息的最后一个数据
				s3c2440_i2c_stop(0);
				break;
			}
			break;
		}
		case STATE_READ:
		{	
			//读出数据
			s3c2440_i2c_xfer_data.msgs->buf[s3c2440_i2c_xfer_data.cur_ptr] = s3c2440_i2c_regs->iicds;
			s3c2440_i2c_xfer_data.cur_ptr++;
			
next_read:		
			if(!isEndData())
			{
				//如果数据没读完，继续发起读操作
				if(isLastData())		//如果即将读的数据是最后一个，那么不发ACK
				{
					s3c2440_i2c_regs->iiccon = 0x2f;
				}
				else
				{
					s3c2440_i2c_regs->iiccon = 0xaf;
				}
				break;
			}
			else if(!isLastMsg())
			{
				//开始处理下一个消息
				s3c2440_i2c_xfer_data.msgs++;
				s3c2440_i2c_xfer_data.cur_msg++;
				s3c2440_i2c_xfer_data.cur_ptr=0;
				s3c2440_i2c_xfer_data.state = STATE_START;

				//发出START信号和发出设备地址
				s3c2440_i2c_start();
				break;
			}
			else
			{
				//如果是最后一个消息的最后一个数据
				s3c2440_i2c_stop(0);
				break;
			}
			break;
		}
		default: break;
	 }

	//清中断
	s3c2440_i2c_regs->iiccon &= ~(S3C2410_IICCON_IRQPEND);

	 return IRQ_HANDLED;
}

static void s3c2440_i2c_init(void)
{

	struct clk *clk;
	clk = clk_get(NULL, "i2c");
	clk_enable(clk);

	//引脚设置
	s3c_gpio_cfgpin(S3C2410_GPE(14),S3C2410_GPE14_IICSCL);
	s3c_gpio_cfgpin(S3C2410_GPE(15),S3C2410_GPE15_IICSDA);
	
	/* bit[6] = 0, IICCLK = PCLK/16
	 * bit[3:0] = 0xf, Tx clock = IICCLK/16
	 * PCLK = 50MHz, IICCLK = 3.125MHz, Tx Clock = 0.195MHz
	 *
	 */
	s3c2440_i2c_regs->iiccon = (1<<7) | (0<<6) | (1<<5) | (0xf);
	s3c2440_i2c_regs->iicadd = 0x10;
	s3c2440_i2c_regs->iicstat = 0x10;
}

static int i2c_bus_s3c2440_init(void)
{
	int ret;
	//硬件初始化
	s3c2440_i2c_regs = ioremap(0x54000000, sizeof(struct s3c2440_i2c_regs));
	s3c2440_i2c_init();
	printk("INT = %d\n", IRQ_IIC);
	ret = request_irq(IRQ_IIC, s3c2440_i2c_xfer_irq, 0, "s3c2440-i2c", NULL);
	if(ret)
	{
		printk("Request_irq error, irq = [%d]\n", IRQ_IIC);
		goto err_iomap;
	}
	init_waitqueue_head(&s3c2440_i2c_xfer_data.wait);
	
	//注册i2c_adapter
	i2c_add_adapter(&s3c2440_i2c_adapter);
	
	return 0;

err_iomap:
	iounmap(s3c2440_i2c_regs);

	return ret;
}

static void i2c_bus_s3c2440_exit(void)
{
	i2c_del_adapter(&s3c2440_i2c_adapter);
	free_irq(IRQ_IIC, NULL);
	iounmap(s3c2440_i2c_regs);
}
MODULE_LICENSE("GPL");
module_init(i2c_bus_s3c2440_init);
module_exit(i2c_bus_s3c2440_exit);


