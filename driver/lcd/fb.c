#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/io.h>

#include <asm/div64.h>

#include <asm/mach/map.h>
#include <mach/regs-lcd.h>
#include <mach/regs-gpio.h>
#include <mach/fb.h>


struct lcd_regs{
	volatile unsigned int lcdcon1;			//0x4D000000
	volatile unsigned int lcdcon2;			//0x4D000004
	volatile unsigned int lcdcon3;			//0x4D000008
	volatile unsigned int lcdcon4;			//0x4D00000C
	volatile unsigned int lcdcon5;			//0x4D000010
	volatile unsigned int lcdsaddr1;		//0x4D000014
	volatile unsigned int lcdsaddr2;		//0x4D000018
	volatile unsigned int lcdsaddr3;		//0x4D00001c
	volatile unsigned int redlut;			//0x4D000020
	volatile unsigned int greenlut;			//0x4D000024
	volatile unsigned int bluelut;			//0x4D000028
	unsigned int a[9];
	volatile unsigned int dithmode;			//0x4D00004c
	volatile unsigned int tpal;			//0x4D000050
	volatile unsigned int lcdintpnd;		//0x4D000054
	volatile unsigned int lcdsrcpnd;		//0x4D000058
	volatile unsigned int lcdintmsk;		//0x4D00005c
	volatile unsigned int tconsel;			//0x4D000060
};

struct gpio_regs{
	volatile unsigned int gpccon;			//0x56000020
	volatile unsigned int gpcdat;			//0x56000024
	volatile unsigned int gpcup;			//0x56000028
	unsigned int a[1];
	volatile unsigned int gpdcon;			//0x56000030
	volatile unsigned int gpddat;			//0x56000034
	volatile unsigned int gpdup;			//0x56000038
};
struct lcd_back_regs{
	volatile unsigned int gpgcon;
	volatile unsigned int gpgdat;
	volatile unsigned int gpgup;
};


struct s3c2440_fb{
	struct fb_info *fb;
	struct lcd_regs *lcd_regs;
	struct gpio_regs *gpio_regs;
	int irq;
	struct clk *clk;
	unsigned long clk_rate;
	unsigned int palette_buffer[256];
	unsigned int pseudo_pal[16];
	struct lcd_back_regs *backlight;
};

static struct s3c2440_fb *s3c2440_info;

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int s3c2440fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;
	
	if(regno > 16)
		return 1;
	
	//use red/green/blue make a color val
	val = chan_to_field(red, &info->var.red);
	val |= chan_to_field(green, &info->var.red);
	val |= chan_to_field(blue, &info->var.red);
	((u32*)(info->pseudo_palette))[regno] = val;
	return 0;
}

struct fb_ops s3c2440fb_ops={
	.fb_setcolreg	= s3c2440fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int s3c2440fb_init(void)
{
	struct fb_info *fbinfo;
	unsigned int lcdcon1;
	int ret = 0;
	unsigned int map_size;
	dma_addr_t dma_handle;
	unsigned int value = 0;
	
	fbinfo = framebuffer_alloc(sizeof(struct s3c2440_fb), NULL);
	if(fbinfo == NULL)
	{
		printk("framebuffer_alloc failure\n");
		return -ENOMEM;
	}
	
	s3c2440_info = fbinfo->par;
	s3c2440_info->fb = fbinfo;
	s3c2440_info->irq = IRQ_LCD;
	s3c2440_info->lcd_regs = ioremap(0x4D000000, sizeof(struct lcd_regs));
	
	strcpy(fbinfo->fix.id, "s3c2440_fb");
	
	//disable video output and LCD control signal
	//lcdcon1 = readl(s3c2440_info->lcd_regs->lcdcon1);
	//writel(lcdcon1 & ~(1), s3c2440_info->lcd_regs->lcdcon1);
	
	s3c2440_info->lcd_regs->lcdcon1 &= ~1;

	fbinfo->fix.type 		= FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux 	= 0;
	fbinfo->fix.smem_len	= 320*240*(16/8);		//display memory space size
	fbinfo->fix.visual		= FB_VISUAL_TRUECOLOR;
	fbinfo->fix.xpanstep	= 0;
	fbinfo->fix.ypanstep	= 0;
	fbinfo->fix.ywrapstep	= 0;
	fbinfo->fix.accel		= FB_ACCEL_NONE;
	fbinfo->fix.line_length	= 240 * 2;
	
	fbinfo->var.xres 			= 320;
	fbinfo->var.yres 			= 240;
	fbinfo->var.bits_per_pixel 	= 16;
	fbinfo->var.nonstd	    	= 0;
	fbinfo->var.activate		= FB_ACTIVATE_NOW;
	//fbinfo->var.accel_flags 	= 0;
	fbinfo->var.vmode	    	= FB_VMODE_NONINTERLACED;
	fbinfo->var.xres_virtual 	= 320;
	fbinfo->var.yres_virtual 	= 240;
	fbinfo->var.transp.offset	= 0;
	fbinfo->var.transp.length	= 0;
	
	fbinfo->var.red.offset		= 11;
	fbinfo->var.green.offset	= 5;
	fbinfo->var.blue.offset 	= 0;
	
	fbinfo->var.red.length		= 5;
	fbinfo->var.green.length	= 6;
	fbinfo->var.blue.length 	= 5;

	//fbinfo->var.height 			= 240;
	//fbinfo->var.width 			= 320;
	
	fbinfo->var.pixclock 		= 166667;
	fbinfo->var.left_margin 	= 20;
	fbinfo->var.right_margin 	= 8;
	fbinfo->var.upper_margin 	= 8;
	fbinfo->var.lower_margin 	= 7;
	fbinfo->var.vsync_len 		= 4;
	fbinfo->var.hsync_len 		= 4;
	

	fbinfo->fbops		    = &s3c2440fb_ops;
	fbinfo->flags		    = FBINFO_FLAG_DEFAULT;
	fbinfo->pseudo_palette  = &s3c2440_info->pseudo_pal;
/*
	ret = request_irq(s3c2440_info->irq, lcd_irq_handler, 0, "LCD", s3c2440_info)
	if(ret)
	{
		printk("2440fb.c, Can't allocate irq %d\n", s3c2440_info->irq);
		ret = -EBUSY;
		goto err_request_irq;
	}
*/
	s3c2440_info->clk = clk_get(NULL, "lcd");
	if (IS_ERR(s3c2440_info->clk))
	{
		printk("failed to get lcd clock source\n");
		ret = PTR_ERR(s3c2440_info->clk);;
		goto err_get_clk;
	}

	clk_enable(s3c2440_info->clk);
	usleep_range(1000, 1000);
	s3c2440_info->clk_rate = clk_get_rate(s3c2440_info->clk);

	//allocate physically contiguous memory
	map_size = PAGE_ALIGN(fbinfo->fix.smem_len);
	fbinfo->screen_base= dma_alloc_writecombine(NULL, map_size, &dma_handle, GFP_KERNEL);
	if(fbinfo->screen_base == 0)
	{
		printk("failed to allocate dma mem space\n");
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	memset(fbinfo->screen_base, 0x00, map_size);
	fbinfo->fix.smem_start = dma_handle; //this is physical address
	fbinfo->screen_size = 320*240*2;

	//set registers
	s3c2440_info->gpio_regs = ioremap(0x56000020, sizeof(struct gpio_regs));
	s3c2440_info->backlight = ioremap(0x56000060,24);

	//backlight off GPG4
	s3c2440_info->backlight->gpgcon &= ~(3 << 4*2);
	s3c2440_info->backlight->gpgcon |= (1 << 4*2);
	s3c2440_info->backlight->gpgdat &= ~(1 << 4);
	
	/*writel( 0xaaaaaaaa,s3c2440_info->gpio_regs->gpccon);
	writel( 0xaaaaaaaa,s3c2440_info->gpio_regs->gpdcon);
	writel( 0xffffffff,s3c2440_info->gpio_regs->gpcup);
	writel( 0xffffffff,s3c2440_info->gpio_regs->gpdup);

	writel(0x00, s3c2440_info->lcd_regs->tconsel);
	writel(0x00, s3c2440_info->lcd_regs->tpal);*/
	s3c2440_info->gpio_regs->gpccon = 0xaaaaaaaa;
	s3c2440_info->gpio_regs->gpdcon = 0xaaaaaaaa;
	s3c2440_info->gpio_regs->gpcup = 0xffffffff;
	s3c2440_info->gpio_regs->gpdup = 0xffffffff;


	//	VCLK = 100M
	//	CLKVAL[17:8]: 	TFT:VCLK=HCLK/[(CLKVAL+1) * 2]=6.4M  
	//					CLKVAL = 6
	//	
	//	PNRMODE[6:5]:	0b11	0x11
	//	BPPMODE[4:1]:	0b1100  0xc		16bpp
	//	
	value = ((6<<8) | (3<<5) | (0x0c<<1));
	//writel(value, s3c2440_info->lcd_regs->lcdcon1);
	s3c2440_info->lcd_regs->lcdcon1 = value;

	//	VBPD[31:24]:	VBPD+1=15		VBPD=14
	//	LINEVAL[23:14]:	LINEVAL+1=240 	LINEVAL=239
	//	VFPD[13:6]:		VFPD+1=12		VFPD=11
	//	VSPW[5:0]:		VSPW+1=3		VSPW=2
	//					
	value = ((14<<24) | (239<<14) | (11<<6) | (2<<0));
	//writel(value, s3c2440_info->lcd_regs->lcdcon2);
	s3c2440_info->lcd_regs->lcdcon2 = value;

	//	HBPD[25:19]		HBPD+1=38		HBPD=37
	//	HOZVAL[18:8]	HOZVAL+1=320	HOZVAL=319
	//	HFPD[7:0]		HFPD+1=20		HFPD=19
	//
	value = ((37<<19) | (319<<8) | (19<<0));
	//writel(value, s3c2440_info->lcd_regs->lcdcon3);
	s3c2440_info->lcd_regs->lcdcon3 = value;

	//	MVAL[15:8]
	//	HSPW[7:0]		HSPW+1=38		HSPW=37
	//
	value = (37<<0);
	//writel(value, s3c2440_info->lcd_regs->lcdcon4);
	s3c2440_info->lcd_regs->lcdcon4 = value;

	//	FRM565[11]		1
	//	INVVCLK[10]		0
	//	INVVLINE[9]		1
	//	INVVFRAME[8]	1
	//	INVVD[7]		0
	//	INVVDEN[6]		0
	//	INVPWREN[5]		0
	//	INVLEND[4]		0
	//	PWREN[3]		0
	//	ENLEND[2]		0
	//	BSWP[1]			0
	//	HWSWP[0]		1
	//
	value = ((1<<11) | (1<<9) | (1<<8) | (1<<0));
	//writel(value, s3c2440_info->lcd_regs->lcdcon5);
	s3c2440_info->lcd_regs->lcdcon5 = value;

	value = (dma_handle >> 1) & ~(3 << 30);
	//writel(value, s3c2440_info->lcd_regs->lcdsaddr1);
	s3c2440_info->lcd_regs->lcdsaddr1 = value;

	value = ((dma_handle + fbinfo->fix.smem_len) >> 1) & 0x1fffff;
	//writel(value, s3c2440_info->lcd_regs->lcdsaddr2);
	s3c2440_info->lcd_regs->lcdsaddr2 = value;

	value = 240;
	//writel(value, s3c2440_info->lcd_regs->lcdsaddr3);
	s3c2440_info->lcd_regs->lcdsaddr3 = value;

	//enable LCD
	//value = readl(s3c2440_info->lcd_regs->lcdcon1);
	//value |= (1<<0);
	//writel(value, s3c2440_info->lcd_regs->lcdcon1);
	s3c2440_info->lcd_regs->lcdcon1 |= (1<<0);

	//enable LCD signal
	//value = readl(s3c2440_info->lcd_regs->lcdcon5);
	//value |= (1<<3);
	//writel(value, s3c2440_info->lcd_regs->lcdcon5);
	s3c2440_info->lcd_regs->lcdcon5 |= (1<<3);

	//backlight on
	s3c2440_info->backlight->gpgdat |= (1 << 4);

	//register framebuffer
	ret = register_framebuffer(fbinfo);
	if (ret < 0)
	{
		printk(KERN_ERR "Failed to register framebuffer device: %d\n",
			ret);
		goto err_register_fb;
	}

	return 0;
 err_register_fb:
 	iounmap(s3c2440_info->gpio_regs);
 	iounmap(s3c2440_info->backlight);
	dma_free_writecombine(NULL, PAGE_ALIGN(fbinfo->fix.smem_len),
			      fbinfo->screen_base, fbinfo->fix.smem_start);
 err_dma_alloc:
 	clk_disable(s3c2440_info->clk);
	clk_put(s3c2440_info->clk);
 	//free_irq(s3c2440_info->irq, s3c2440_info);
 err_get_clk:
 	iounmap(s3c2440_info->lcd_regs);
	framebuffer_release(fbinfo);
	
	return ret;
}

static void s3c2440fb_exit(void)
{
	framebuffer_release(s3c2440_info->fb);
	iounmap(s3c2440_info->gpio_regs);
 	iounmap(s3c2440_info->backlight);
	iounmap(s3c2440_info->lcd_regs);
	//free_irq(s3c2440_info->irq, s3c2440_info);
	clk_put(s3c2440_info->clk);
	clk_disable(s3c2440_info->clk);
	dma_free_writecombine(NULL, PAGE_ALIGN(s3c2440_info->fb->fix.smem_len),
			      s3c2440_info->fb->screen_base, s3c2440_info->fb->fix.smem_start);
}


MODULE_LICENSE("GPL");
module_init(s3c2440fb_init);
module_exit(s3c2440fb_exit);

