/*
 * linux/drivers/media/video/jz4755_cim.c -- Ingenic Camera Controller driver
 *
 * Copyright (C) 2005-2010, Ingenic Semiconductor Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/videobuf-core.h>
#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/jz_cim.h>
#include <asm/cacheflush.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/thread_info.h>
#include <asm/uaccess.h>
#include <asm/jzsoc.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <media/videobuf-dma-contig.h>

MODULE_DESCRIPTION("V4L2 driver for Jz4755 CIM");
MODULE_AUTHOR("zwu <zwu@ingenic.cn>");
MODULE_LICENSE("GPL");

/* module res->ters */
static int video_nr = 0;       /* video device minor (-1 ==> auto assign) */

static int buf_cnt = -1;	/* frame buffer count */

#define CIM_MAJOR_VERSION 0
#define CIM_MINOR_VERSION 6
#define CIM_RELEASE 0
#define CIM_VERSION \
	KERNEL_VERSION(CIM_MAJOR_VERSION, CIM_MINOR_VERSION, CIM_RELEASE)

#define CIM_INTR_EOF_EN
#define CIM_INTR_OF_EN
#define CIM_INTR_TLB_EN	//enable TLB error interrupt

static unsigned debug = 9;	//normal debug (0: disable) 
static unsigned idebug = 9;	//interrupt debug (0: disable)
#define dprintk(dev, level, fmt, arg...)	\
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

#define iprintk(fmt, arg...)	\
	v4l2_dbg(0, idebug, &dev->v4l2_dev, fmt, ## arg)

#define line	(printk("-->%s L%d\n", __func__, __LINE__))

/*
 * CIM DMA descriptor
 */
struct videobuf_dma_contig_memory {
	u32 magic;
	void *vaddr;
	dma_addr_t dma_handle;
	unsigned long size;
	int is_userptr;
};

struct desc_v_p_addr{
	unsigned long vaddr;
	unsigned long paddr;
};

#define v_p_addr_count 32
struct desc_v_p_addr desc_v_p_addr_1[v_p_addr_count];

struct cim_desc {
	u32 nextdesc;   // Physical address of next desc
#if defined(CONFIG_SOC_JZ4760B) || defined(CONFIG_SOC_JZ4770) || defined(CONFIG_SOC_JZ4775) || defined(CONFIG_SOC_JZ4780)
	u32 frameid;    // Frame ID
	u32 framebuf;   // Physical address of frame buffer, when SEP=1, it's y framebuffer
#else
	u32 framebuf;   // Physical address of frame buffer, when SEP=1, it's y framebuffer
	u32 frameid;    // Frame ID
#endif
	u32 dmacmd;     // DMA command, when SEP=1, it's y cmd
	/* only used when SEP = 1 */
	u32 cb_frame;
	u32 cb_len;
	u32 cr_frame;
	u32 cr_len;
};

struct cim_fmt {
	char  *name;
	u32   fourcc;	/* v4l2 format id */
	int   bpp;	/* bit per pixel */
};

static struct cim_fmt formats[] = {
	{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.bpp    = 16,
	},
	{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.bpp    = 16,
	},
	{
		.name     = "4:2:2, planar, Y-U-V",
		.fourcc   = V4L2_PIX_FMT_YUV422P,
		.bpp    = 16,
	},
	{
		.name     = "4:2:0, planar, Y-U-V",
		.fourcc   = V4L2_PIX_FMT_YUV420,	//YU12
		.bpp    = 12,
	},
	{
		.name     = "4:2:0, two-plane, Y-UV",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.bpp    = 12,
	},
	{
		.name     = "4:4:4, packed or planar ???",
		.fourcc   = V4L2_PIX_FMT_YUV444,
		.bpp    = 24,
	},
};


struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct cim_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct cim_fmt        *fmt;
};

struct cim_dmaqueue {
        struct list_head	  active;

        /* thread for generating video stream*/
        struct task_struct        *kthread;
        wait_queue_head_t         wq;
        /* Counters to control fps rate */
        int                        frame;
        int                        ini_jiffies;
};

/* supported controls */
static struct v4l2_queryctrl cim_qctrl[] = {
        {
                .id            = V4L2_CID_AUDIO_VOLUME,
                .name          = "Volume",
                .minimum       = 0,
                .maximum       = 65535,
                .step          = 65535/100,
                .default_value = 65535,
                .flags         = V4L2_CTRL_FLAG_SLIDER,
                .type          = V4L2_CTRL_TYPE_INTEGER,
        }, {
                .id            = V4L2_CID_BRIGHTNESS,
                .type          = V4L2_CTRL_TYPE_INTEGER,
                .name          = "Brightness",
                .minimum       = 0,
                .maximum       = 255,
                .step          = 1,
                .default_value = 127,
                .flags         = V4L2_CTRL_FLAG_SLIDER,
        }
};

static LIST_HEAD(cim_devlist);

struct cim_reg {
        unsigned int cfg;       //CIMCFG
        unsigned int ctrl;      //CIMCR
        unsigned int ctrl2;     //CIMCR2
        unsigned int size;      //CIMSIZE
        unsigned int offs;      //CIMOFFSET
};

#define TLB_ENTRY_NUM	(2048)
struct cim_tlb_entry {
	unsigned int	val:1,		//valid flag
			reserved:11,
			pfn:20;		//physical frame number
};

static struct cim_desc local_dma_desc[2 * VIDEO_MAX_FRAME] __attribute__ ((aligned(sizeof(struct cim_desc))));

struct cim_dev {
	int			id;		/* CIM0 or CIM1 */
	int			irq;
	struct cim_reg		regs;

	struct jz_sensor_desc	*sensor;
	int			dma_stop;	/* stop dma or not */
	struct cim_desc 	*dma_desc;

	int			mb;		/* tile mode for yuv420 */
	int			planar;		/* planar or packed format */
	struct cim_tlb_entry	*tlb_entry;

	struct cim_buf_info	buf[VIDEO_MAX_FRAME];
	int			pid;		/* process id */

	struct resolution_info 	res;		/* request resolution */

        struct list_head        cim_devlist;
        struct v4l2_device      v4l2_dev;

        spinlock_t              slock;
        struct mutex            mutex;
        int			users;

        /* various device info */
        struct video_device     *vdev;

        struct cim_dmaqueue     vidq;

        /* input number */
        int                     input;

        /* control 'registers' */
        int                     qctl_regs[ARRAY_SIZE(cim_qctrl)];

	void 			*priv;	/* point to struct cim_fh -- by ylyuan */
};

struct cim_fh {
	struct cim_dev            *dev;

	/* video capture */
	struct cim_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	int			   input; 	/* Input Number on bars */
};


static unsigned int dmacmd_intr_flag = 0
#ifdef CIM_INTR_SOF_EN
	| CIM_CMD_SOFINT
#endif
#ifdef CIM_INTR_EOF_EN
	| CIM_CMD_EOFINT
#endif
#ifdef CIM_INTR_EEOF_EN
	| CIM_CMD_EEOFINT
#endif
	;

#define is_sep()	( cim->regs.cfg & CIM_CFG_SEP )
#define is_yuv422()	( (cim->regs.cfg & CIM_CFG_DF_MASK) == CIM_CFG_DF_YUV422 )

/*==========================================================================
 * File operations
 *========================================================================*/
int cim_start_flag = 0;
static int cim_open(struct file *filp);
static ssize_t cim_read(struct file *file, char __user *data, size_t count, loff_t *ppos);
static int cim_mmap(struct file *file, struct vm_area_struct *vma);
static int cim_set_function(struct cim_dev *cim, int function);
static int cim_set_resolution(struct cim_dev *cim, struct resolution_info *res);

#ifdef CONFIG_VIDEO_CIM_VA
static void cim_dump_tlb(struct cim_dev *cim);
static unsigned int cim_refill_tlb(struct cim_dev *cim, unsigned int vaddr);
static unsigned int cim_get_tlb_entry(struct cim_dev *cim, unsigned int vaddr);
static unsigned int get_phy_addr(unsigned int vaddr, int pid);
static void cim_read_tlb_content(struct cim_dev *cim, int idx);
#endif

/*==========================================================================
 * CIM debug functions
 *========================================================================*/

void cim_print_regs(void)
{
	printk("=========================================\n");
	printk("REG_CIM_CFG \t\t= \t0x%08x\n",REG_CIM_CFG);
	printk("REG_CIM_CTRL \t= \t0x%08x\n",REG_CIM_CTRL);
	printk("REG_CIM_STATE \t= \t0x%08x\n",REG_CIM_STATE);
	printk("REG_CIM_IID \t\t= \t0x%08x\n",REG_CIM_IID);
	printk("REG_CIM_DA \t\t= \t0x%08x\n",REG_CIM_DA);
	printk("REG_CIM_FA \t\t= \t0x%08x\n",REG_CIM_FA);
	printk("REG_CIM_FID \t\t= \t0x%08x\n",REG_CIM_FID);
	printk("REG_CIM_CMD \t\t= \t0x%08x\n",REG_CIM_CMD);
	printk("REG_CIM_SIZE \t= \t0x%08x\n",REG_CIM_SIZE);
	printk("REG_CIM_OFFSET \t= \t0x%08x\n",REG_CIM_OFFSET);
	printk("=========================================\n");
}

/*====================================================================================
 * sensor support
 *===================================================================================*/

static LIST_HEAD(sensor_list);
static DEFINE_MUTEX(sensor_lock);

int jz_sensor_register(struct jz_sensor_desc *desc)
{
	if (desc == NULL)
		return -EINVAL;

	desc->sensor_id = 0xffff;

	if (desc->bus_width < 8)
		desc->bus_width = 8;

	mutex_lock(&sensor_lock);
	list_add_tail(&desc->list, &sensor_list);
	mutex_unlock(&sensor_lock);

	desc->ops->sensor_set_power(desc, 1);

	return 0;
}

static int cim_scan_sensor(struct cim_dev *cim)
{
	
	struct jz_sensor_desc *desc;

	cim->sensor = NULL;

	mutex_lock(&sensor_lock);
	list_for_each_entry(desc, &sensor_list, list) {
		printk(KERN_INFO "%s: attaching %s to %s...\n", __func__, desc->name, cim->vdev->name);

		if(desc->ops->sensor_probe(desc) != 0) {	/* probe fail */
			printk(KERN_ERR "%s: fail to probe sensor %s\n", __func__, desc->name);
			mutex_unlock(&sensor_lock);
			return -ENODEV;
		}

		if (desc->cim_id == cim->id) {
			cim->sensor = desc;
			printk(KERN_INFO "%s: attached %s to %s...\n", __func__, desc->name, cim->vdev->name);
			mutex_unlock(&sensor_lock);
			return 0;
		}
	}

	mutex_unlock(&sensor_lock);
	return -ENODEV;
}

#ifdef CONFIG_VIDEO_CIM_VA
/*====================================================================================
 * virtual address support
 *===================================================================================*/
static unsigned int get_phy_addr(unsigned int vaddr, int pid)
{
	struct task_struct *task;
	unsigned int addr = vaddr & (PAGE_SIZE-1);
	unsigned int paddr = 0;
	pgd_t   *pgdir;
#ifdef CONFIG_PGTABLE_4
	pud_t	*pudir;
#endif
     	pmd_t   *pmdir;
      	pte_t   *pte;

	task = pid ? find_task_by_vpid(pid) : current;

//	printk("task pid = %d\n", task->pid);
//	printk("current task(%d)\n",current->pid);
//	if (current->mm && current->mm->pgd)
//		printk("current task(%d)'s pgd is %p\n",current->pid,current->mm->pgd);

	pgdir=pgd_offset(task->mm,vaddr);
	if(pgd_none(*pgdir)||pgd_bad(*pgdir))
		return   -EINVAL;

#ifdef CONFIG_PGTABLE_4
	pudir=pud_offset(pgdir,vaddr);
	if(pud_none(*pudir)||pud_bad(*pudir))
		return   -EINVAL;

	pmdir=pmd_offset(pudir,vaddr);
	if(pmd_none(*pmdir)||pmd_bad(*pmdir))
		return   -EINVAL;
#else
	pmdir=pmd_offset((pud_t *)pgdir,vaddr);
	if(pmd_none(*pmdir)||pmd_bad(*pmdir))
		return   -EINVAL;
#endif
	pte=pte_offset(pmdir,vaddr);

	if(pte_present(*pte))
	{
		paddr =  addr | (pte_pfn(*pte)<<PAGE_SHIFT);
		//pte_page(*pte);
	}

	return paddr;
}

static void cim_read_tlb_content(struct cim_dev *cim, int idx)
{
#if 0
	__cim_set_tlb_idx(cim->id, idx);
	dprintk(cim, 1, "%s: Index=%d, Content=0x%08x\n", __func__, REG_CIM_TINX(cim->id), REG_CIM_TCNT(cim->id));
#endif
}

static void cim_read_tlb_all(struct cim_dev *cim)
{
#if 0
	int idx;
	for (idx=0; idx<8; idx++) {
		cim_read_tlb_content(cim, idx);
	}
#endif
}

static int cim_set_tlb_entry(struct cim_dev *cim, unsigned int vaddr)
{
#if 0
	unsigned int paddr = get_phy_addr(vaddr, 0);
	unsigned int pfn = paddr >> PAGE_SHIFT;
	unsigned int vpn = vaddr >> PAGE_SHIFT;
	unsigned int offs = ((unsigned int)cim->buf[0].vaddr >> PAGE_SHIFT) * 4;
	struct cim_tlb_entry *entry = (struct cim_tlb_entry *)((unsigned char *)(cim->tlb_entry) - offs + vpn * 4);
	entry->pfn = pfn;
	entry->val = 1;
	printk("%s: L%d: set entry %p to 0x%08x\n", __func__, __LINE__, entry, *entry);
	return 0;
#endif
}

static void cim_enable_tlb(struct cim_dev *cim)
{
#if 0
	struct cim_tlb_entry *tlb_entry = cim->tlb_entry;
	int id = cim->id;
	unsigned int tba = 0;	//CIMTC.TBA[31:2]
	unsigned int vpn;

	vpn = cim->buf[0].vaddr >> PAGE_SHIFT;
	tba = (CPHYSADDR(tlb_entry) - vpn * 4) >> 2;	//word aligned
	printk("%s L%d: buf[0].vaddr=0x%08x\n", __func__, __LINE__, cim->buf[0].vaddr);
	printk("%s: vpn=0x%08x, tba=0x%08x, tba<<2=0x%08x\n", __func__, vpn, tba, tba<<2);
	printk("%s: tlb_entry=%p, CPHYSADDR(tlb_entry)=0x%08x\n", __func__, tlb_entry, CPHYSADDR(tlb_entry));
	__cim_set_tlb_addr(id, tba);
	__cim_enable_tlb_error_intr(id);
	__cim_reset_tlb(id);
	__cim_unreset_tlb(id);
	__cim_enable_tlb(id);
#endif
}

static void cim_init_tlb(struct cim_dev *cim, struct v4l2_buffer *b)
{
#if 0
	struct cim_tlb_entry *tlb_entry = cim->tlb_entry;
	unsigned int vaddr = b->m.userptr;
	unsigned int size = b->length;
	int page_num = size >> PAGE_SHIFT;
	int i;

	for (i = 0; i < page_num; i++) {
		cim_set_tlb_entry(cim, vaddr + i * PAGE_SIZE);
	}
	//printk("i=%d, page_num=%d\n", i, page_num);

	dma_cache_wback((unsigned int)tlb_entry, sizeof(struct cim_tlb_entry) * TLB_ENTRY_NUM);
	dma_cache_wback_inv((unsigned int)tlb_entry, sizeof(struct cim_tlb_entry) * TLB_ENTRY_NUM);
#endif
}

static unsigned int cim_get_tlb_entry(struct cim_dev *cim, unsigned int vaddr)
{
#if 0
	unsigned int addr = vaddr & (PAGE_SIZE-1);
	unsigned int vpn = vaddr >> PAGE_SHIFT;
	struct cim_tlb_entry *entry = (struct cim_tlb_entry *)(CKSEG1ADDR(REG_CIM_TC(cim->id) & ~0x3) + vpn * 4);
	unsigned int paddr = 0;

	if (entry->val)
		paddr = (entry->pfn << PAGE_SHIFT) | addr;

	return paddr;
#endif
}

static unsigned int cim_refill_tlb(struct cim_dev *cim, unsigned int vaddr)
{
#if 0
	unsigned int paddr = get_phy_addr(vaddr, cim->pid);
	unsigned int pfn = paddr >> PAGE_SHIFT;
	unsigned int vpn = vaddr >> PAGE_SHIFT;
	struct cim_tlb_entry *entry = (struct cim_tlb_entry *)(CKSEG1ADDR(REG_CIM_TC(cim->id) & ~0x3) + vpn * 4);

	printk("%s: tba=0x%08x, vpn=0x%08x\n", __func__, (REG_CIM_TC(cim->id) & ~0x3), vpn);
	printk("%s: vaddr=0x%08x, entry=%p(0x%08x), paddr=0x%08x\n", 	\
		__func__, vaddr, entry, (*entry), paddr);
	if (entry->val)
		printk("%s: valid entry. vaddr=0x%08x, entry=%p(0x%08x), paddr=0x%08x\n",	\
			 __func__, vaddr, entry, (*entry), paddr);
	else {
		entry->pfn = pfn;
		entry->val = 1;
	}
	/* FIXME: flush cache */
	dma_cache_wback((unsigned int)cim->tlb_entry, sizeof(struct cim_tlb_entry) * TLB_ENTRY_NUM);
	__cim_disable_tlb(cim->id);
	__cim_reset_tlb(cim->id);
	__cim_unreset_tlb(cim->id);
	__cim_enable_tlb(cim->id);

	return 0;
#endif
}

static void cim_dump_tlb(struct cim_dev *cim)
{
#if 0
	struct cim_tlb_entry *entry = cim->tlb_entry;
	int i;

	for (i = 0; i < TLB_ENTRY_NUM; i++, entry++) {
		if (entry->val != 0)
			printk("## tlb_entry[%d]=0x%08x\n", i, (*entry));
	}
#endif
}

static int cim_set_buffer(struct cim_dev *cim, struct v4l2_buffer *b)
{
	struct cim_buf_info *buf = &cim->buf[b->index];
	int frmsize = (cim->res.width * cim->res.height * cim->res.bpp + 7) >> 3;	//request frame size

	if (b->memory != V4L2_MEMORY_USERPTR) {
		printk("%s L%d: invalid v4l2 memory type!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (b->length < frmsize) {
		printk("%s L%d: too small! request 0x%x, but only 0x%x available.\n", __func__, __LINE__, frmsize, b->length);
		return -EINVAL;
	}
	if (b->m.userptr & (PAGE_SIZE - 1))	//page aligned or not
		return -EINVAL;

	buf->vaddr = b->m.userptr;
	buf->size = b->length;

	buf->paddr = get_phy_addr(buf->vaddr, 0);
	if (buf->paddr == 0)
		return -EINVAL;

	printk("%s L%d: buf[%d] vaddr=0x%08x, paddr=0x%08x, size=0x%x\n", __func__, __LINE__, b->index, buf->vaddr, buf->paddr, buf->size);

	cim_init_tlb(cim, b);

	//cim_dump_tlb();
	return 0;
}
#endif // CONFIG_VIDEO_CIM_VA

static int cim_set_dma(struct cim_dev *cim, struct videobuf_buffer *vbuf)
{
	struct cim_fh *fh = cim->priv;
	struct cim_desc *desc = NULL;
#ifdef CONFIG_VIDEO_CIM_VA
	u32 frmbuf = vbuf->baddr;
#else
	u32 frmbuf = videobuf_to_dma_contig(vbuf);
#endif
	u32 ylen = fh->width * fh->height;

	if (vbuf == NULL)
		return -EINVAL;

	dprintk(cim, 1, "%s L%d: i=%d, vbuf=%p, vbuf->size=0x%lx\n", __func__, __LINE__, vbuf->i, vbuf, vbuf->size);
	dprintk(cim, 1, "%s L%d: w=%d, h=%d, ylen=0x%x, fourcc=%s, planar=%d, mb=%d\n", __func__, __LINE__, 
				fh->width, fh->height, ylen, (char *)&fh->fmt->fourcc, cim->planar, cim->mb);

	desc = &cim->dma_desc[vbuf->i];

	//init dma descriptor
	desc->nextdesc = virt_to_phys(&cim->dma_desc[vbuf->i + 1]);
	desc->frameid  = vbuf->i;
	desc->framebuf = frmbuf;
	if (cim->planar == 0) {
		desc->dmacmd   = (vbuf->size>>2) | dmacmd_intr_flag;	//packed mode
	} else {
		desc->dmacmd   = (ylen >> 2) | dmacmd_intr_flag;	//planar mode

		switch (fh->fmt->fourcc) {
		case V4L2_PIX_FMT_YUV444:
			desc->cb_frame = frmbuf + ylen;
			desc->cb_len = ylen >> 2;
			desc->cr_frame = frmbuf + ylen + ylen;
			desc->cr_len = ylen >> 2;
			break;
		case V4L2_PIX_FMT_YUV422P:
			desc->cb_frame = frmbuf + ylen;
			desc->cb_len = (ylen / 2) >> 2;
			desc->cr_frame = frmbuf + ylen + ylen / 2;
			desc->cr_len = (ylen / 2) >> 2;
			break;
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_NV12:
			desc->cb_frame = frmbuf + ylen;
			desc->cb_len = (ylen / 4) >> 2;
			if (cim->mb == 0)
				desc->cr_frame = frmbuf + ylen + ylen / 4;
			else
				desc->cr_frame = frmbuf + ylen + 8;	// yuv420 tile mode
			desc->cr_len = (ylen / 4) >> 2;
			break;
		default:
			printk("%s L%d: invalid format %s\n", __func__, __LINE__, (char *)&fh->fmt->fourcc);
			break;
		}
	}
	desc->dmacmd |= CIM_CMD_STOP;

	dma_cache_wback_inv((unsigned long)desc, sizeof(struct cim_desc));

	return 0;
}

static void cim_enable_dma(struct cim_dev *cim, struct videobuf_buffer *vbuf)	//by ylyuan
{
	struct cim_desc *desc = NULL;
	int idx = 0;

	if (vbuf == NULL)
		idx = 0;
	else
		idx = vbuf->i;

	dprintk(cim, 1, "%s L%d: buf[%d]\n", __func__, __LINE__, idx);
	desc = &cim->dma_desc[idx];
	dma_cache_wback_inv((unsigned long)&cim->dma_desc[0], buf_cnt * sizeof(struct cim_desc));
	//enable cim
	__cim_set_da(virt_to_phys(desc));
	__cim_clear_state();	// clear state register
	__cim_reset_rxfifo();	// resetting rxfifo
	__cim_unreset_rxfifo();
	__cim_enable_dma();
	__cim_enable();
}

/*==========================================================================
 * CIM Module operations
 *========================================================================*/
static void cim_set_fmt(struct cim_dev *cim, struct cim_fmt *fmt, __u32 priv)
{
	__u8 sep = 0;		//package mode as default
	__u8 packing = 0;
	__u8 tmp;
	__u8 tile = 0;		//tile mode for yuv420

	//CIMCFG.DSM 
	__cim_enable_gated_clock_mode();	//Gate Clock Mode

	/*
	 * set input format
	 */
	//CIMCFG.DF
#if defined(CONFIG_VIDEO_CIM_IN_FMT_YUV444)
	__cim_input_data_format_select_YUV444();
#elif defined(CONFIG_VIDEO_CIM_IN_FMT_YUV422)
	__cim_input_data_format_select_YUV422();
#elif defined(CONFIG_VIDEO_CIM_IN_FMT_ITU656)
	__cim_input_data_format_select_ITU656();
#else
#if !defined(CONFIG_VIDEO_CIM_OUT_FMT_BYPASS)
#error "Unsupported input format!"
#endif
#endif
	//CIMCFG.ORDER
	__cim_set_input_data_stream_order(0); /* YCbCr or Y0CbY1Cr */

	/*
	 * set output format
	 * 
	 * set packing mode for output format
	 *
	 * CIMCFG.PACK[6:4]		data in RxFiFo
	 * 	0		 11 22 33 44  or  Y0 Cb Y1 Cr 
	 * 	1		 22 33 44 11  or  Cb Y1 Cr Y0
	 * 	2		 33 44 11 22  or  Y1 Cr Y0 Cb
	 * 	3		 44 11 22 33  or  Cr Y0 Cb Y1
	 * 	4		 44 33 22 11  or  Cr Y1 Cb Y0
	 * 	5		 33 22 11 44  or  Y1 Cb Y0 Cr
	 * 	6		 22 11 44 33  or  Cb Y0 Cr Y1
	 * 	7		 11 44 33 22  or  Y0 Cr Y1 Cb
	 */
	//CIMCR2.CSC
	if (fmt->fourcc == cim->sensor->fourcc)
		__cim_enable_bypass_func();	
	else {
		switch (fmt->fourcc) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YUV422P:
			//__cim_csc_yuv422(id);
			break;
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_NV12:
			//__cim_csc_yuv420(id);
			break;
		default:
			break;
		}
	}

	cim_get_fmt_priv(priv, tmp, tmp, packing, sep);
	printk("%s L%d: priv=0x%08x, sep=%c, packing=%d\n", __func__, __LINE__, priv, sep, packing);

	//CIMCFG.PACK
	__cim_set_data_packing_mode(packing); 
	//CIMCFG.SEP
	if (sep == V4L2_FRM_FMT_PACK) {	//package mode
		//__cim_disable_sep(id);
		cim->planar = 0;
	} else	{			//planar mode
		//__cim_enable_sep(id);
		cim->planar = 1;
	}
	//CIMCR.MBEN
	tile = (sep == V4L2_FRM_FMT_TILE) ? 1 : 0;
	if (tile) {
		//__cim_enable_mb(id);
		cim->mb = 1;
	}
}

static void cim_config(struct cim_dev *cim)
{
	REG_CIM_CFG  = cim->regs.cfg;
	REG_CIM_CTRL = cim->regs.ctrl;
	REG_CIM_SIZE = cim->regs.size;
	REG_CIM_OFFSET = cim->regs.offs;

#ifdef CIM_SAFE_DISABLE
	__cim_enable_vdd_intr();
#else
	__cim_disable_vdd_intr();
#endif

#ifdef CIM_INTR_SOF_EN
	__cim_enable_sof_intr();
#else
	__cim_disable_sof_intr();
#endif

#ifdef CIM_INTR_EOF_EN
	__cim_enable_eof_intr();
#else
	__cim_disable_eof_intr();
#endif

#ifdef CIM_INTR_STOP_EN
	__cim_enable_stop_intr();
#else
	__cim_disable_stop_intr();
#endif

#ifdef CIM_INTR_OF_EN
	__cim_enable_rxfifo_overflow_intr();
#else
	__cim_disable_rxfifo_overflow_intr();
#endif

#ifdef CIM_INTR_EEOF_EN
	__cim_set_eeof_line(100);
	__cim_enable_eeof_intr();
#else
	__cim_set_eeof_line(0);
	__cim_disable_eeof_intr();
#endif

#ifdef CONFIG_VIDEO_CIM_FSC
	//__cim_enable_framesize_check_error_intr(id);
#else
	//__cim_disable_framesize_check_error_intr(id);
#endif
}

static void cim_init_config(struct cim_dev *cim)
{
	memset(&cim->regs, 0, sizeof(cim->regs));

	cim->regs.cfg &= ~CIM_CFG_DMA_BURST_TYPE_MASK;
	cim->regs.cfg |= CIM_CFG_DMA_BURST_INCR8;
	cim->regs.ctrl = CIM_CTRL_DMA_SYNC | CIM_CTRL_FRC_1;
	
}

void cim_power_off(void)                                                                                               
{                                                                                                                      
	__cpm_stop_cim();                                                                                              
}                                                                                                                      
                                                                                                                       
void cim_power_on(void)                                                                                                
{                                                                                                                      
	__cpm_start_cim();                                                                                             
}

inline void cim_start(struct cim_dev *cim)
{
	struct jz_sensor_desc *desc = cim->sensor;
	cim->dma_stop = 0;
	cim_start_flag = 1;
	cim_power_on();
	desc->ops->sensor_set_power(desc, 0);
}

static void cim_stop(struct cim_dev *cim)
{
	struct jz_sensor_desc *desc = cim->sensor;
	cim->dma_stop = 1;
	cim_start_flag = 0;
	__cim_disable();
	__cim_clear_state();

	desc->ops->sensor_set_power(desc, 1);
	cim_power_off();
}

static int cim_device_init(struct cim_dev *cim)
{
	struct jz_sensor_desc *desc = cim->sensor;
	__gpio_as_cim_8bit();
	cim_stop(cim);

	cim_start(cim);	//start cim clk

	desc->ops->sensor_init(desc);

	//set sensor working mode
	cim_set_function(cim, SENSOR_MODE_PREVIEW);	//preview mode

	cim_init_config(cim);
	cim_config(cim);

	return 0;
}

/*==========================================================================
 * Interrupt handler
 *========================================================================*/

static irqreturn_t cim_irq_handler(int irq, void *data)
{
	struct cim_dev *dev		= (struct cim_dev *)data;
//	struct cim_dmaqueue *vidq	= &dev->vidq;
	struct cim_fh *fh		= (struct cim_fh *)dev->priv;
	struct videobuf_queue *q	= &fh->vb_vidq;
	struct videobuf_buffer *buf 	= NULL;
	u32 state, state_back;
	unsigned long flags;
	int iid = 0;

	state = state_back = REG_CIM_STATE;
	if (state & CIM_STATE_DMA_SOF) {
		state &= ~CIM_STATE_DMA_SOF;
		iprintk("sof intrrupt occured\n");
	}

	if (state & CIM_STATE_DMA_STOP) {
		__cim_disable();
		__cim_reset_rxfifo();	// resetting rxfifo
		__cim_unreset_rxfifo();
		__cim_clear_state();

		state &= ~CIM_STATE_DMA_STOP;
		iprintk("stop intrrupt occured\n");

	//	return IRQ_HANDLED;
	}

	if (state & CIM_STATE_VDD) {
		state &= ~CIM_STATE_VDD;
		iprintk("cim disable done!\n");
	}

	if (state & CIM_STATE_DMA_EEOF) {
		state &= ~CIM_STATE_DMA_EEOF;
		iprintk("eeof intrrupt occured!\n");
	}
#if 0
	if (state & CIM_STATE_FSE) {
		state &= ~CIM_STATE_FSE;
		printk("%s: framesize check error! CIMFS=0x%08x\n", __func__, REG_CIM_FS(dev->id));
	}
#endif
#ifdef CONFIG_VIDEO_CIM_VA
	if (state & CIM_STATE_TLBE) {
		unsigned int irq_fid = 0;       //interrupt frame id
		unsigned int irq_fb = 0;        //interrupt frame buffer

		state &= ~CIM_STATE_TLBE;
		printk("%s: TLB error interrupt\n", __func__);
		/*
		 * Refill TLB
		 */
		irq_fid = __cim_get_iid();
		irq_fb = dev->buf[irq_fid].vaddr;
		iprintk("%s: irq_fid=%d, irq_fb=0x%08x\n", __func__, irq_fid, irq_fb);

		cim_read_tlb_content(dev, 0);
		cim_refill_tlb(dev, irq_fb);
	}
#endif	// CONFIG_VIDEO_CIM_VA
	if (state & CIM_STATE_DMA_EOF) {
		spin_lock_irqsave(&dev->slock, flags);

		iid = __cim_get_iid();
		state &= ~CIM_STATE_DMA_EOF;
		iprintk("eof intrrupt occured! iid=%d\n", iid);

		buf = q->bufs[iid];
		iprintk("%s L%d: buf[%d]=%p\n", __func__, __LINE__, iid, buf);
		if (buf != NULL) {
			buf->state = VIDEOBUF_DONE;
			wake_up(&buf->done);
		}
		spin_unlock_irqrestore(&dev->slock, flags);

		return IRQ_HANDLED;
	}

	state = state_back;
	if ( 	(state & CIM_STATE_RXF_OF)
		//|| (state & CIM_STATE_Y_RF_OF)
		//|| (state & CIM_STATE_CB_RF_OF)
		//|| (state & CIM_STATE_CR_RF_OF)
	   ) {
		if (state & CIM_STATE_RXF_OF)
			printk("OverFlow interrupt!\n");
#if 0
		if (state & CIM_STATE_Y_RF_OF)
			printk("Y overflow interrupt!!!\n");

		if (state & CIM_STATE_CB_RF_OF)
			printk("Cb overflow interrupt!!!\n");

		if (state & CIM_STATE_CR_RF_OF)
			printk("Cr overflow interrupt!!!\n");
#endif
		REG_CIM_STATE &= ~CIM_STATE_VDD;
		__cim_disable();
		__cim_reset_rxfifo();	// resetting rxfifo
		__cim_unreset_rxfifo();
		__cim_clear_state();
		__cim_enable();

		return IRQ_HANDLED;
	}

	__cim_clear_state();
 	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/

#define vid_limit 	16	//capture memory limit in megabytes
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	int bit_per_pix = 24;
	struct cim_fh  *fh = vq->priv_data;
	struct cim_dev *dev  = fh->dev;
	struct cim_fmt *fmt = fh->fmt;

	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_UYVY:
		bit_per_pix = 16;
		break;
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420:
		bit_per_pix = 12;
		break;
	case V4L2_PIX_FMT_GREY:
		bit_per_pix = 8;
		break;
	default:
		bit_per_pix = 24;
		break;
	}

	*size = (fh->width * fh->height * bit_per_pix) >> 3;

	if (0 == *count)
		*count = VIDEO_MAX_FRAME;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=0x%x\n", __func__,
		*count, *size);

	buf_cnt = *count;

	dprintk(dev, 1, "%s L%d: width=%d, height=%d, size=%d, count=%d\n", __func__, __LINE__, fh->width, fh->height, *size, *count);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct cim_buffer *buf)
{
	struct cim_fh  *fh = vq->priv_data;
	struct cim_dev *dev  = fh->dev;
	struct videobuf_buffer *vb = &buf->vb;

	dprintk(dev, 1, "%s L%d: vb=%p, state=%i\n", __func__, __LINE__, vb, buf->vb.state);

	if (in_interrupt())
		BUG();

        /* This waits until this buffer is out of danger, i.e., until it is no
         * longer in STATE_QUEUED or STATE_ACTIVE */
        videobuf_waiton(vb, 0, 0);
        videobuf_dma_contig_free(vq, vb);

	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1920
#define norm_maxh() 1280
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	int bit_per_pix = 24;
	struct cim_fh     *fh  = vq->priv_data;
	struct cim_fmt    *fmt = fh->fmt;
	struct cim_dev    *dev = fh->dev;
	struct cim_buffer *buf = container_of(vb, struct cim_buffer, vb);
	int rc;

	dprintk(dev, 1, "%s L%d: buf[%d]=%p, field=%d\n", __func__, __LINE__, vb->i, vb, field);

	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh()) {
		dprintk(dev, 1, "%s L%d: invalid resolution! w=%d, h=%d\n", __func__, __LINE__, fh->width, fh->height);
		return -EINVAL;
	}

	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_UYVY:
		bit_per_pix = 16;
		break;
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420:
		bit_per_pix = 12;
		break;
	case V4L2_PIX_FMT_GREY:
		bit_per_pix = 8;
		break;
	default:
		bit_per_pix = 24;
		break;
	}

	buf->vb.size = (fh->width * fh->height * bit_per_pix) >> 3;

	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size) {
		dprintk(dev, 1, "%s L%d: invalid size! baddr=0x%08lx, bsize=%d, size=%ld\n", __func__, __LINE__, buf->vb.baddr, buf->vb.bsize, buf->vb.size);
		return -EINVAL;
	}

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field  = field;

#ifndef CONFIG_VIDEO_CIM_VA
	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}
#endif
	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

fail:
	free_buffer(vq, buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct cim_buffer    *buf  = container_of(vb, struct cim_buffer, vb);
	struct cim_fh        *fh   = vq->priv_data;
	struct cim_dev       *dev  = fh->dev;
	struct cim_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);

	cim_set_dma(dev, vb);	//by ylyuan
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct cim_buffer   *buf  = container_of(vb, struct cim_buffer, vb);
	struct cim_fh       *fh   = vq->priv_data;
	struct cim_dev      *dev  = (struct cim_dev *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops cim_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct cim_fh  *fh  = priv;
	struct cim_dev *dev = fh->dev;
	strcpy(cap->driver, "cim");
	strcpy(cap->card, "cim");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = CIM_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct cim_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cim_fh *fh = priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->bpp) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return (0);
}

static struct cim_fmt *get_format(struct v4l2_format *f)
{
	struct cim_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == ARRAY_SIZE(formats))
		return NULL;

	return &formats[k];
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct cim_fh  *fh  = priv;
	struct cim_dev *dev = fh->dev;
	struct cim_fmt *fmt = NULL;
	enum v4l2_field field;
	unsigned int maxw, maxh;
	fmt = get_format(f);
	if (!fmt) {
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid.\n",
			f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_NONE;
	} else if (V4L2_FIELD_NONE != field) {
		dprintk(dev, 1, "Field type invalid.\n");
		return -EINVAL;
	}
	maxw  = norm_maxw();
	maxh  = norm_maxh();

	f->fmt.pix.field = field;
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);

//	cim_set_fmt(dev, fmt, f->fmt.pix.priv);	//change format (yuv, rgb, raw...) by ylyuan
	REG_CIM_CFG = 0x00021446;
	dev->res.width = f->fmt.pix.width;
	dev->res.height = f->fmt.pix.height;
	dev->res.bpp = fmt->bpp;

	cim_set_resolution(dev, &dev->res);

	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->bpp) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cim_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;

	int ret = vidioc_try_fmt_vid_cap(file, fh, f);
	if (ret < 0)
		return ret;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dprintk(fh->dev, 1, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	fh->fmt           = get_format(f);
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	dprintk(fh->dev, 1, "%s L%d: fmt=%s, fourcc=%s, w=%d, h=%d\n", __func__, __LINE__, fh->fmt->name, (char *)&fh->fmt->fourcc, fh->width, fh->height);

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct cim_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cim_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cim_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cim_fh  *fh = priv;
	struct cim_dev *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;
	struct videobuf_buffer *buf = NULL;
	int ret = 0;

	ret = videobuf_dqbuf(&fh->vb_vidq, p, file->f_flags & O_NONBLOCK);
	if (ret)
		return ret;

	//setup future dma
	if (!list_empty(&q->stream)) {
		buf = list_entry(q->stream.next,
				 struct videobuf_buffer, stream);
		cim_set_dma(dev, buf);
		if (!dev->dma_stop)
			cim_enable_dma(dev, buf);
	}

	return ret;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct cim_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cim_fh  *fh = priv;
	struct cim_dev *dev = fh->dev;
	int ret  = 0;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	ret = videobuf_streamon(&fh->vb_vidq);

	if (ret == 0) {
#ifdef CONFIG_VIDEO_CIM_VA
		cim_enable_tlb(dev);
#endif
		cim_enable_dma(dev, NULL);
	}

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cim_fh  *fh = priv;
	struct cim_dev *dev = fh->dev;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	cim_stop(dev);

	return videobuf_streamoff(&fh->vb_vidq);
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
//	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);

	return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct cim_fh *fh = priv;
	struct cim_dev *dev = fh->dev;

	*i = dev->input;

	return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct cim_fh *fh = priv;
	struct cim_dev *dev = fh->dev;

	if (i > 0)
		return -EINVAL;

	dev->input = i;

	return (0);
}

static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cim_qctrl); i++)
		if (qc->id && qc->id == cim_qctrl[i].id) {
			memcpy(qc, &(cim_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct cim_fh *fh = priv;
	struct cim_dev *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(cim_qctrl); i++)
		if (ctrl->id == cim_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct cim_fh *fh = priv;
	struct cim_dev *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(cim_qctrl); i++)
		if (ctrl->id == cim_qctrl[i].id) {
			if (ctrl->value < cim_qctrl[i].minimum ||
			    ctrl->value > cim_qctrl[i].maximum) {
				return -ERANGE;
			}
			dev->qctl_regs[i] = ctrl->value;
			return 0;
		}
	return -EINVAL;
}

static int cim_test_reg(struct cim_dev *cim, struct cim_reg_info *argp)
{
	struct cim_reg_info *info = argp;

	printk("%s L%d: reg=0x%08x,val=0x%08x,w/r=%s\n", __func__, __LINE__, info->reg, info->val, ((info->ops & CIM_OPS_REG) == CIM_WR_REG)?"write":"read");

	if (info->reg < 0xa0000000)
		info->reg |= 0xa0000000;

	if ((info->ops & CIM_OPS_REG) == CIM_WR_REG) {
		REG32(info->reg) = info->val;
		printk("%s L%d: wr, REG32(0x%08x)=0x%08x\n", __func__, __LINE__, info->reg, REG32(info->reg));
	} else {
		info->val = REG32(info->reg);
		printk("%s L%d: rd, reg=0x%08x,val=0x%08x\n", __func__,__LINE__,info->reg,info->val);
	}

	return 0;
}

static long vidioc_default(struct file *file, void *fh,	int cmd, void *arg)
{
	struct cim_dev *cim = ((struct cim_fh *)fh)->dev;
	struct jz_sensor_desc *desc = cim->sensor;
	struct cim_v_p_addr_info *v_p_addr_info = arg;
	void __user *argp = (void __user *)arg;
	int ret = 0;
	int temp = 0;
	
	switch (cmd) {
#ifdef CONFIG_VIDEO_CIM_VA
	case VIDIOC_CIM_S_MEM:
		cim_set_buffer(cim, arg);
		break;
#endif
	case VIDIOC_CIM_RW_REG:
		cim_test_reg(cim, arg);
		break;

	case VIDIOC_CIM_SET_FOUCS:
		if (desc->ops->sensor_set_foucs == NULL) {
			printk("sensor set foucs error !!!!\n");
			return -ENODEV;
		}
		ret = desc->ops->sensor_set_foucs(desc);
		break;

	case VIDIOC_CIM_GET_V_P_ADDR:
		for(temp = 0; temp < 32; temp++){
			if(desc_v_p_addr_1[temp].vaddr == v_p_addr_info->vaddr){
				v_p_addr_info->paddr = desc_v_p_addr_1[temp].paddr;
				break;
			}
		}
		
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/
static int cim_open(struct file *filp)
{
	struct cim_dev *cim = video_drvdata(filp);
	struct video_device *vdev = cim->vdev;
	struct cim_fh *fh = NULL;
	int retval = 0;
	cim->pid = current->pid;	/* process id */
	//printk("****************%s,%d\n",__func__,__LINE__);
	/* scan sensor */
	cim_power_on();
	cim_scan_sensor(cim);
	if (!cim->sensor)
		return -ENODEV;

	cim_device_init(cim);	//start cim clk

	mutex_lock(&cim->mutex);
	cim->users++;

	if (cim->users > 1) {
		cim->users--;
		mutex_unlock(&cim->mutex);
		return -EBUSY;
	}

	dprintk(cim, 1, "open /dev/video%d type=%s users=%d\n", vdev->num,
		v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], cim->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		cim->users--;
		retval = -ENOMEM;
	}
	mutex_unlock(&cim->mutex);

	if (retval)
		return retval;

	filp->private_data = fh;
	fh->dev      = cim;
	cim->priv    = fh;	//by ylyuan

//	fh->vb_vidq.dev = &cim->vdev->dev;	//for dev_dbg() -> dev_driver_string()
	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &formats[0];
	fh->width    = 320;
	fh->height   = 240;

	videobuf_queue_dma_contig_init(&fh->vb_vidq, &cim_video_qops,
			NULL, &cim->slock, fh->type, V4L2_FIELD_NONE,
			sizeof(struct cim_buffer), fh);
	dprintk(cim, 1, "%s L%d: cim=%p, fh=%p\n", __func__, __LINE__, cim, fh);
	return 0;
}

static ssize_t cim_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct cim_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static int cim_enable_image_mode(struct cim_dev *cim, int image_w,int image_h,int width,int height)
{
	int voffset,hoffset, half_words_per_line = 0;

	voffset = (height - image_h) / 2;
	voffset &= ~0x1;      /* must be even */

	if (is_yuv422()) {
		hoffset = (width - image_w) / 2;
		hoffset &= ~0x1;      /* must be even */

		half_words_per_line = image_w;
	} else {
		hoffset = (width - image_w) / 2;
		hoffset = hoffset * 3 / 4 * 2;

		half_words_per_line = image_w * 3 / 2; /* image_w must be even */
	}
	__cim_set_line(image_h);
	__cim_set_pixel(half_words_per_line);

	__cim_set_v_offset(voffset);
	__cim_set_h_offset(hoffset);

	__cim_enable_size_func();

	printk("enable image mode (real size %d x %d) - %d x %d\n",width,height,image_w,image_h);
	return 0;
}

static int cim_disable_image_mode(struct cim_dev *cim)
{
	__cim_disable_size_func();
	//REG_CIM_CTRL &= ~CIM_CTRL_WIN_EN;
	printk("disable image mode\n");
	return 0;
}

static int cim_set_resolution(struct cim_dev *cim, struct resolution_info *res)
{
	struct jz_sensor_desc *desc = cim->sensor;
	int frmsize = (res->width * res->height * res->bpp + 7) >> 3;	//Byte
	int max_frmsize = (desc->cap_res.width * desc->cap_res.height * desc->cap_res.bpp + 7) >> 3;
	int nr = desc->table_nr;
	struct resolution_info local_res;
	int idx, i;
	if(cim_start_flag == 0)
		cim_start(cim);
	if (frmsize > max_frmsize) {
		printk("ERROR! Capture size is too large!\n");
		return -EINVAL;
	}

	if (desc->ops->sensor_set_resolution== NULL) {
		return -ENODEV;
	}

	for(idx = 0; idx < nr; idx++) {
	
		if ((desc->resolution_table[idx].width == res->width) && (desc->resolution_table[idx].height == res->height))
			break;
	}

	if (idx >= nr) {
		printk("size not found! use image mode!\n");
		for(i = nr-1; i >= idx; i--) {
			if (desc->resolution_table[i].width > res->width && desc->resolution_table[i].height > res->height)
				break;
		}

		cim_enable_image_mode(cim, 
			res->width,
			res->height,
			desc->resolution_table[i].width,
			desc->resolution_table[i].height);

		local_res.width = desc->resolution_table[i].width;
		local_res.height = desc->resolution_table[i].height;
		local_res.bpp = res->bpp;
		desc->ops->sensor_set_resolution(desc, &local_res);

		printk("%s L%d: cim in, w=%d h=%d, cim out, w=%d h=%d\n", __func__, __LINE__,
			desc->resolution_table[i].width,
			desc->resolution_table[i].height,
			res->width,
			res->height);
	} else {
		cim_disable_image_mode(cim);

		local_res.width = desc->resolution_table[idx].width;
		local_res.height = desc->resolution_table[idx].height;
		local_res.bpp = res->bpp;
		desc->ops->sensor_set_resolution(desc, res);
	}

	//set frame size
#if defined(CONFIG_VIDEO_CIM_IN_FMT_YUV444)
	local_res.bpp = 24;
#elif defined(CONFIG_VIDEO_CIM_IN_FMT_YUV422) || defined(CONFIG_VIDEO_CIM_IN_FMT_ITU656)
	local_res.bpp = 16;
#else
	/* other formats: bypass */
#endif
#if 0
	__cim_set_fs_v_size(cim->id, local_res.height-1);	//vertical size
	__cim_set_fs_h_size(cim->id, local_res.width-1);	//horizontal size
	__cim_set_fs_bpp(cim->id, (local_res.bpp>>3)-1);	//Bytes per pixel

	__cim_enable_framesize_check(cim->id);
	__cim_enable_arif(cim->id);	//enable auto recovery for incomplete frame
#endif
	dprintk(cim, 1, "%s L%d: w=%d, h=%d, bpp=%d\n", __func__, __LINE__, res->width, res->height, res->bpp);
	return 0;
}

static int cim_set_function(struct cim_dev *cim, int func)
{
	
	struct jz_sensor_desc *desc = cim->sensor;

	if(desc == NULL)
		return -ENODEV;
	if(desc->ops->sensor_set_function == NULL)
		return -ENODEV;

	return desc->ops->sensor_set_function(desc, func);
}

static unsigned int cim_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cim_fh        *fh = file->private_data;
	struct cim_dev       *cim = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(cim, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int cim_close(struct file *file)
{
        struct cim_fh *fh = file->private_data;
        struct cim_dev *cim = fh->dev;
	int minor = video_devdata(file)->minor;
        videobuf_stop(&fh->vb_vidq);
        videobuf_mmap_free(&fh->vb_vidq);

        mutex_lock(&cim->mutex);
        cim->users--;
	if (cim->users == 0) {
		cim_stop(cim);
		cim->sensor = NULL;
	        kfree(fh);
	}
        mutex_unlock(&cim->mutex);

        dprintk(cim, 1, "close called (minor=%d, users=%d)\n", minor, cim->users);

        return 0;
}

static int cim_mmap(struct file *file, struct vm_area_struct *vma)
{
        struct cim_fh  *fh = file->private_data;
	struct cim_dev *cim = fh->dev;
        int ret;
	int temp;

        dprintk(cim, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);
        ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

#if 1	//write by zwu 
	for(temp = 0;temp < 32;temp++)
	{
		if(fh->vb_vidq.bufs[temp]->baddr == vma->vm_start)
		{
			desc_v_p_addr_1[temp].vaddr = vma->vm_start;
			desc_v_p_addr_1[temp].paddr = 
					((struct videobuf_dma_contig_memory *)(fh->vb_vidq.bufs[temp]->priv))->dma_handle;
			
			break;
		}
	}
#endif

	dprintk(cim, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
                (unsigned long)vma->vm_start,
                (unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
                ret);

        return ret;
}

static const struct v4l2_file_operations cim_fops = {
	.owner		= THIS_MODULE,
	.open           = cim_open,
	.release        = cim_close,
	.read           = cim_read,
	.poll		= cim_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = cim_mmap,
};

static const struct v4l2_ioctl_ops cim_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_enum_fmt_vid_cap= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,
	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_dqbuf		= vidioc_dqbuf,
	.vidioc_s_std		= vidioc_s_std,
	.vidioc_enum_input	= vidioc_enum_input,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl,
	.vidioc_streamon	= vidioc_streamon,
	.vidioc_streamoff	= vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf		= vidiocgmbuf,
#endif
	.vidioc_default		= vidioc_default,
};

static struct video_device cim_template = {
	.fops           = &cim_fops,
	.ioctl_ops 	= &cim_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
//	.tvnorms              = V4L2_STD_525_60,
//	.current_norm         = V4L2_STD_NTSC_M,
};

static int cim_probe(struct platform_device *pdev)
{
	struct video_device *vdev;
	struct cim_dev *cim = NULL;
	struct resource *irqres = NULL;
	int ret;

	if ((pdev->id > 1) || (pdev->id < 0)) {
		printk(KERN_ERR "%s: Invalid ID: %d\n", __func__, pdev->id);
		return -EINVAL;
	}

	cim = kzalloc(sizeof(*cim), GFP_KERNEL);
	if(!cim)
		return -ENOMEM;

	cim->id = pdev->id;
	cim->dma_desc = &local_dma_desc[cim->id * VIDEO_MAX_FRAME];

#ifdef CONFIG_VIDEO_CIM_VA
	cim->tlb_entry = kzalloc(sizeof(struct cim_tlb_entry) * TLB_ENTRY_NUM, GFP_KERNEL);
	if(cim->tlb_entry == NULL) {
		printk("%s L%d: fail to allocate tlb_entry!\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto free_dev;
	}
	*(unsigned int *)&cim->tlb_entry = CKSEG1ADDR(cim->tlb_entry);	//uncached
	printk("%s L%d: tlb_entry=%p\n", __func__, __LINE__, cim->tlb_entry);
#endif

	if (pdev->resource == NULL || pdev->num_resources < 2) {
		printk(KERN_ERR "%s: Invalid resource\n", __func__);
		ret = -ENXIO;
		goto free_dev;
	}

	irqres = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irqres) {
		printk(KERN_ERR "%s: failed to get IRQ resource\n", __func__);
		ret = -ENXIO;
		goto free_dev;
	}
	cim->irq = irqres->start;	/* IRQ_CIM or IRQ_CIM1 */

	snprintf(cim->v4l2_dev.name, sizeof(cim->v4l2_dev.name), "%s-%03d", "cim", pdev->id);
	ret = v4l2_device_register(NULL, &cim->v4l2_dev);
	if (ret)
		goto free_dev;

        /* init video dma queues */
        INIT_LIST_HEAD(&cim->vidq.active);
        init_waitqueue_head(&cim->vidq.wq);

	/* initialize locks */
	spin_lock_init(&cim->slock);
	mutex_init(&cim->mutex);

	if ((ret = request_irq(cim->irq, cim_irq_handler, IRQF_DISABLED, cim->v4l2_dev.name, cim))) {
		printk(KERN_ERR "%s: fail to request irq (%d)!\n", __func__, cim->irq);
		goto unreg_dev;
	}

	ret = -ENOMEM;
	vdev = video_device_alloc();
	if (!vdev)
		goto unreg_dev;

	*vdev = cim_template;
	vdev->num = pdev->id;
	vdev->debug = 0;
	snprintf(vdev->name, sizeof(vdev->name), "%s (%i)", "cim", vdev->num);

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		goto rel_vdev;

	video_set_drvdata(vdev, cim);

	platform_set_drvdata(pdev, vdev);

	cim->vdev = vdev;
	v4l2_info(&cim->v4l2_dev, "%s registered as /dev/video%d\n", vdev->name, video_nr);

	video_nr++;

	return 0;

rel_vdev:
	video_device_release(vdev);
unreg_dev:
	v4l2_device_unregister(&cim->v4l2_dev);
free_dev:
	kfree(cim->tlb_entry);
	kfree(cim);
	return ret;
}

static int cim_remove(struct platform_device *pdev)
{
	struct video_device *vdev = platform_get_drvdata(pdev);
	struct cim_dev *cim = video_get_drvdata(vdev);

	v4l2_info(&cim->v4l2_dev, "unregistering /dev/video%d\n", vdev->num);

	video_unregister_device(cim->vdev);
	v4l2_device_unregister(&cim->v4l2_dev);
	if (cim->tlb_entry)
		kfree(cim->tlb_entry);
	kfree(cim);

	return 0;
}

static struct platform_driver jz_cim_driver = {
	.probe	= cim_probe,
	.remove	= cim_remove,
	.driver	= {
		.name	= "jz_cim",
	},
};

static int __init cim_init(void)
{
	int ret = 0;
	
	ret = platform_driver_register(&jz_cim_driver);

	return ret;
}

static void __exit cim_exit(void)
{
	platform_driver_unregister(&jz_cim_driver);
}

//late_initcall(cim_init);// CIM driver should not be inited before PMU driver inited.
module_init(cim_init);
module_exit(cim_exit);
