#ifndef __JZ4760_C__
#define __JZ4760_C__
/*
 * Author: River <zwang@ingenic.cn>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/jzsoc.h>

#include "musb_core.h"

static inline void jz_musb_phy_enable(void)
{
	printk(KERN_INFO "jz4760: Enable USB PHY.\n");

	__cpm_enable_otg_phy();

	/* Wait PHY Clock Stable. */
	udelay(300);

	return;
}

static inline void jz_musb_phy_disable(void)
{
	printk(KERN_INFO "jz4760: Disable USB PHY.\n");

	__cpm_suspend_otg_phy();

	return;
}

static inline void jz_musb_phy_reset(void)
{
	REG_CPM_USBPCR |= USBPCR_POR;
	udelay(30);
	REG_CPM_USBPCR &= ~USBPCR_POR;

	udelay(300);

	return;
}

static inline void jz_musb_set_device_only_mode(void)
{
	printk(KERN_INFO "jz4760: Device only mode.\n");

	/* Device Mode. */
	REG_CPM_USBPCR &= ~(1 << 31);

	REG_CPM_USBPCR |= USBPCR_VBUSVLDEXT;

	return;
}

static inline void jz_musb_set_normal_mode(void)
{
	printk(KERN_INFO "jz4760: Normal mode.\n");

	__gpio_as_otg_drvvbus();

	/* OTG Mode. */
	REG_CPM_USBPCR |= (1 << 31);

	REG_CPM_USBPCR &= ~((1 << 24) | (1 << 23) | (1 << 20));

	REG_CPM_USBPCR |= ((1 << 28) | (1 << 29));
	return;
}

static inline void jz_musb_init(struct musb *musb)
{
	cpm_set_clock(CGU_OTGCLK, 12 * 1000 * 1000);
	udelay(100);
	
	/* fil */
	REG_CPM_USBVBFIL = 0x80;

	/* rdt */
	REG_CPM_USBRDT = 0x96;

	/* rdt - filload_en */
	REG_CPM_USBRDT |= (1 << 25);

	/* TXRISETUNE & TXVREFTUNE. */
	REG_CPM_USBPCR &= ~0x3f;
	REG_CPM_USBPCR |= 0x35;

	/* enable tx pre-emphasis */
        REG_CPM_USBPCR |= 0x40;

        /* most DC leave of tx */
#if defined(CONFIG_SOC_JZ4760) || defined(CONFIG_SOC_JZ4760B)
        REG_CPM_USBPCR |= 0xf;
#elif defined(CONFIG_SOC_JZ4770)
        //FIXME:
        //the same adjustment to jz4770 ???
#endif


	if (is_host_enabled(musb)) {
		jz_musb_set_normal_mode();
	}else
		jz_musb_set_device_only_mode();

	jz_musb_phy_reset();

	return;
}

int musb_platform_set_mode(struct musb *musb, u8 musb_mode)
{
	return 0;
}

void musb_platform_enable(struct musb *musb)
{
	jz_musb_phy_enable();

	return;
}

void musb_platform_disable(struct musb *musb)
{
	jz_musb_phy_disable();

	return;
}

static void jz_musb_set_vbus(struct musb *musb, int is_on)
{
	u8		devctl;
	/* HDRC controls CPEN, but beware current surges during device
	 * connect.  They can trigger transient overcurrent conditions
	 * that must be ignored.
	 */

	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	if (is_on) {

		musb->is_active = 1;
		musb->xceiv->default_a = 1;
		musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
		devctl |= MUSB_DEVCTL_SESSION;

		
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
#ifdef GPIO_OTG_VBUS_PIN		
		__gpio_as_output(GPIO_OTG_VBUS_PIN);
		__gpio_set_pin(GPIO_OTG_VBUS_PIN);
#endif
		MUSB_HST_MODE(musb);
		
		
	} else {

		musb->is_active = 0;

		/* NOTE:  we're skipping A_WAIT_VFALL -> A_IDLE and
		 * jumping right to B_IDLE...
		 */

		musb->xceiv->default_a = 0;
		musb->xceiv->state = OTG_STATE_B_IDLE;
		devctl &= ~MUSB_DEVCTL_SESSION;

		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
#ifdef GPIO_OTG_VBUS_PIN		
		__gpio_as_output(GPIO_OTG_VBUS_PIN);
		__gpio_clear_pin(GPIO_OTG_VBUS_PIN);
#endif
		MUSB_DEV_MODE(musb);
	}
	
	DBG(1, "VBUS %s, devctl %02x "
		/* otg %3x conf %08x prcm %08x */ "\n",
		otg_state_string(musb),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

/* ---------------------- OTG ID PIN Routines ---------------------------- */

static struct timer_list otg_id_pin_stable_timer;

static unsigned int read_gpio_pin(unsigned int pin, unsigned int loop)
{
	unsigned int t, v;
	unsigned int i;

	i = loop;

	v = t = 0;

	while (i--) {
		t = __gpio_get_pin(pin);
		if (v != t)
			i = loop;

		v = t;
	}

	return v;
}

static void do_otg_id_pin_state(struct musb *musb)
{
	unsigned int default_a;
#ifndef CONFIG_JZ4760_HTB80
	unsigned int pin = read_gpio_pin(GPIO_OTG_ID_PIN, 100);
#else
	unsigned int pin = 1; /* always B */
#endif

	default_a = !pin;

	musb->xceiv->default_a = default_a;

	jz_musb_set_vbus(musb, default_a);

	if (pin) {
		/* B */
#ifdef CONFIG_USB_MUSB_PERIPHERAL_HOTPLUG
			__gpio_unmask_irq(OTG_HOTPLUG_PIN);
#endif
#ifndef CONFIG_JZ4760_HTB80
			__gpio_as_irq_fall_edge(GPIO_OTG_ID_PIN);
#endif
	} else {
		/* A */
		if (is_otg_enabled(musb)) {
#ifdef CONFIG_USB_MUSB_PERIPHERAL_HOTPLUG
			__gpio_mask_irq(OTG_HOTPLUG_PIN); // otg's host mode not support hotplug
#endif
#ifndef CONFIG_JZ4760_HTB80
			__gpio_as_irq_rise_edge(GPIO_OTG_ID_PIN);
#endif
		}
	}

	return;
}

static void otg_id_pin_stable_func(unsigned long data)
{
	struct musb *musb = (struct musb *)data;

	do_otg_id_pin_state(musb);

	return;
}

static irqreturn_t jz_musb_otg_id_irq(int irq, void *data)
{
	mod_timer(&otg_id_pin_stable_timer, GPIO_OTG_STABLE_JIFFIES + jiffies);

	return IRQ_HANDLED;
}

static int otg_id_pin_setup(struct musb *musb)
{
	int rv;

	/* Update OTG ID PIN state. */
	do_otg_id_pin_state(musb);
#ifndef CONFIG_JZ4760_HTB80
	setup_timer(&otg_id_pin_stable_timer, otg_id_pin_stable_func, (unsigned long)musb);

	rv = request_irq(GPIO_OTG_ID_IRQ, jz_musb_otg_id_irq,
				IRQF_DISABLED, "otg-id-irq", musb);
	if (rv) {
		pr_err("Failed to request OTG_ID_IRQ.\n");
		return rv;
	}
#endif

	return rv;
}

static void otg_id_pin_cleanup(struct musb *musb)
{
#ifndef CONFIG_JZ4760_HTB80
	free_irq(GPIO_OTG_ID_IRQ, "otg-id-irq");
	del_timer(&otg_id_pin_stable_timer);
#endif

	return;
}

/* ---------------------------------------------------------------- */

int __init musb_platform_init(struct musb *musb)
{
	int rv = 0;

	musb->xceiv = otg_get_transceiver();
	if (!musb->xceiv) {
		pr_err("HS USB OTG: no transceiver configured\n");
		return -ENODEV;
	}

	musb->b_dma_share_usb_irq = 1;
	musb->board_set_vbus = jz_musb_set_vbus;

	jz_musb_init(musb);

	/* host mode and otg(host) depend on the id pin */
	if (is_host_enabled(musb))
		rv = otg_id_pin_setup(musb);

	return rv;
}

int musb_platform_exit(struct musb *musb)
{
	jz_musb_phy_disable();

	if (is_host_enabled(musb))
		otg_id_pin_cleanup(musb);

	return 0;
}

#endif
