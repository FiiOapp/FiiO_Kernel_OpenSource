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
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/system.h>

MODULE_DESCRIPTION("keyboard driver for Jz4755 CIM");
MODULE_AUTHOR("zwu <zwu@ingenic.cn>");
MODULE_LICENSE("GPL");

#define KEYBOARD_MAJOR 0
#define KEYBOARD_NAME "keyboard"
#define MEMDEV_NUM 1

#define GPF10 (32*5+10)
#define GPF11 (32*5+11)
#define GPF12 (32*5+12)
#define GPF13 (32*5+13)

#define KEYBOARD_SW3	(IRQ_GPIO_0+GPF11)
#define KEYBOARD_SW4	(IRQ_GPIO_0+GPF12)
#define KEYBOARD_SW5	(IRQ_GPIO_0+GPF10)
#define KEYBOARD_SW6	(IRQ_GPIO_0+GPF13)

struct keyboard_dev_t {
	struct cdev cdev;
};
struct button_irq_desc {

	int irq;
	int pin;
	int number;
	char *name;
};

static struct button_irq_desc button_irqs [] = {                                                                                             

	{KEYBOARD_SW3, GPF11, 0,"keyborad_sw3"}, //key_cancel

	{KEYBOARD_SW4, GPF12, 1,"keyborad_sw4"}, //key_up

	{KEYBOARD_SW5, GPF10, 2,"keyborad_sw5"}, //key_down

	{KEYBOARD_SW6, GPF13, 3,"keyborad_sw6"}, //key_ok
};

dev_t devno;
static int keyboard_major = KEYBOARD_MAJOR;
struct keyboard_dev_t *keyboard_dev;
struct class *mydriver_class;
static DECLARE_WAIT_QUEUE_HEAD(button_waitq);
static char key_values [] = {'0', '0', '0', '0'};
static volatile int ev_press = 0;

int keyboard_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	printk("--->%s,%d\n",__func__,__LINE__);
	return 0;
}

ssize_t keyboard_read( struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
	unsigned long err;                                                                                                                   

	//printk("--->%s,%d\n",__func__,__LINE__);
	if (!ev_press) {
		if (filp->f_flags & O_NONBLOCK){
			printk("keyboard_read err !\n");
			return -EAGAIN;
		}
		else
			wait_event_interruptible(button_waitq, ev_press);
	}   

	ev_press = 0;
	err = copy_to_user(buf, (const void *)key_values, sizeof(key_values));

	memset(key_values, 0, sizeof(key_values));

	return err ? -EFAULT : min(sizeof(key_values), count);
}

ssize_t keyboard_write( struct file *filp, const char __user *buf, size_t size, loff_t *ppos )
{
	int ret = 0;
	printk("--->%s,%d\n",__func__,__LINE__);

	return ret;
}

irqreturn_t keyboard_handler(int irq, void *dev_id) {
		
	int down;
	struct button_irq_desc *button_irq = (struct button_irq_desc *)dev_id;
	down = !__gpio_get_pin(button_irq->pin);
	if (down != (key_values[button_irq->number] & 1)) {

		key_values[button_irq->number] = '0' + down;
		ev_press = 1;
		wake_up_interruptible(&button_waitq);
	}

	return (IRQ_HANDLED); 
} 

int keyboard_open( struct inode *node, struct file *filp )
{
	int i = 0,ret = 0;
	/* request irq */
	for (i = 0; i < sizeof(button_irqs)/sizeof(button_irqs[0]); i++) {

		if (button_irqs[i].irq < 0) {                                                                                                
			continue;
		}
		ret = request_irq(button_irqs[i].irq, keyboard_handler, IRQ_TYPE_EDGE_BOTH, 
				button_irqs[i].name, (void *)&button_irqs[i]);
		if(ret != 0){
			printk("request_irq err!\n");
			return ret;
		}
	}
#if 0
	/* enable irq */
	for(i = 0; i<sizeof(button_irqs)/sizeof(button_irqs[0]); i++){
		enable_irq(button_irqs[i].irq);
	}
#endif
	return 0;
}
static int keyboard_close(struct inode *inode, struct file *file)
{
	int i = 0;
	printk("--->%s,%d\n",__func__,__LINE__);

	for (i = 0; i < sizeof(button_irqs)/sizeof(button_irqs[0]); i++) {
		if (button_irqs[i].irq < 0) {
			printk("button irq < 0\n");
			continue;
		}
		/*释放中断号,并注销中断处理函数*/
		free_irq(button_irqs[i].irq, (void *)&button_irqs[i]);
	}

	return 0;
}

/*file operations */
static const struct file_operations keyboard_fops = {
	.owner = THIS_MODULE,
	.open = keyboard_open,
	.release = keyboard_close,
	.read = keyboard_read,
	.write = keyboard_write,
	.ioctl = keyboard_ioctl,
};


static int __init keyboard_init(void)
{
	int ret = 0, result = 0, err = 0;

	printk("--->%s,%d\n",__func__,__LINE__);

	keyboard_dev = kmalloc(sizeof(struct keyboard_dev_t), GFP_KERNEL);
	memset(keyboard_dev, 0, sizeof(struct keyboard_dev_t));
	if (!keyboard_dev)
	{
		result = -ENOMEM;
		goto fail_malloc;
	}

	devno = MKDEV(keyboard_major,0); 

	/*require cdev number*/
	if(keyboard_major) {
		result = register_chrdev_region(devno, MEMDEV_NUM, KEYBOARD_NAME);
	}
	else {
		result = alloc_chrdev_region(&devno, 0, MEMDEV_NUM, KEYBOARD_NAME);
		keyboard_major = MAJOR(devno);
		printk("keyboard_major=%d\n",keyboard_major);
	}
	if (result < 0) {
		printk("register_chrdev_region err!!\n");
		return result;
	}
	
	devno = MKDEV(keyboard_major,0);

	/* Register for a class, so that mdev can establish equipment node in the /dev/ directory */
	mydriver_class = class_create(THIS_MODULE, KEYBOARD_NAME); 

	/* creat dev node ，the name of node is KEYBOARD_NAME */
    	device_create(mydriver_class, NULL, devno, NULL, KEYBOARD_NAME);

	/* init cdev */
	cdev_init(&keyboard_dev->cdev, &keyboard_fops);	

	/* register cdev */	
	err = cdev_add(&keyboard_dev->cdev, devno, 1);	
	if (err){
		printk(KERN_NOTICE "Error %d!!\n", err); 
		return -1;
	}
	
	__gpio_as_irq_fall_edge(GPF11);
	__gpio_as_irq_fall_edge(GPF12);
	__gpio_as_irq_fall_edge(GPF10);
	__gpio_as_irq_fall_edge(GPF13);

	return ret;

	fail_malloc: 
	unregister_chrdev_region(devno, 1);
	return result;
}

static void __exit keyboard_exit(void)
{
	int i = 0;
	cdev_del(&keyboard_dev->cdev);
	kfree(keyboard_dev);
	unregister_chrdev_region(devno, MEMDEV_NUM);//注意释放的设备号个数一定要和申请的设备号个数保存一致

	for (i = 0; i < sizeof(button_irqs)/sizeof(button_irqs[0]); i++) {
		if (button_irqs[i].irq < 0) {
			printk("button irq < 0\n");
			continue;
		}
		/*释放中断号,并注销中断处理函数*/
		free_irq(button_irqs[i].irq, (void *)&button_irqs[i]);
	}
}

module_init(keyboard_init);
module_exit(keyboard_exit); 
