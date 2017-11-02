/* The On-Chip ADC driver of the Ingenic application processor.
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 * All rights reserved.
 *
 * Apply to JZ4750L, JZ4755 ......
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  NOTE: This driver does not yet support subtractive ADC mode, which means
 *  you can do only one measurement per read request.
 */
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/jz4750l-sadc.h>

#include <asm/jzsoc.h>

/* register */
#define ADENA_OFF   0x00
#define ADCFG_OFF   0x04
#define ADCTRL_OFF  0x08
#define ADSTATE_OFF 0x0c
#define ADSAME_OFF  0x10
#define ADWAIT_OFF  0x14
#define ADTCH_OFF   0x18
#define ADBDAT_OFF  0x1c
#define ADSDAT_OFF  0x20
#define ADFLT_OFF   0x24
#define ADCLK_OFF   0x28

#define ADENA_SADCINEN  BIT(0)

#define ADSTATE_SRDY    BIT(0)

struct jz_adc {
	struct resource *mem;
	void __iomem *io_base;

	int irq;
	int irq_base;

	spinlock_t lock;
	atomic_t clk_ref;

	struct platform_device *pdev;
};

static void jz_adc_clk_enable(struct jz_adc *adc)
{
	if (atomic_inc_return(&adc->clk_ref) == 1) {
		__cpm_start_sadc();
		/* ADCLK:
		 *   CLKDIV - ADC work clock = External Clock / (CLKDIV + 1)
		 *   CLKDIV_10 - us_clk = (CLKDIV_10 + 1) * 100K ???
		 * if CLKDIV == 3, CLKDIV_10 = 39
		 */
		writel((39 << 16) | 2, adc->io_base + ADCLK_OFF);
	}
}

static void jz_adc_clk_disable(struct jz_adc *adc)
{
	if (atomic_dec_return(&adc->clk_ref) == 0)
		__cpm_stop_sadc();
}

static void jz_adc_dev_ctl(struct jz_adc *adc, unsigned int id, int enabled)
{
	uint8_t val;
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);

	val = readb(adc->io_base + ADENA_OFF);
	if (enabled)
		writeb(val | BIT(id), adc->io_base + ADENA_OFF);
	else
		writeb(val & ~BIT(id), adc->io_base + ADENA_OFF);

	spin_unlock_irqrestore(&adc->lock, flags);
}

static void jz_adc_irq_ctl(struct jz_adc *adc, unsigned int irq, int masked)
{
	uint8_t val;
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);

	val = readb(adc->io_base + ADCTRL_OFF);
	if (masked)
		writeb(val | BIT(irq), adc->io_base + ADCTRL_OFF);
	else
		writeb(val & ~BIT(irq), adc->io_base + ADCTRL_OFF);

	spin_unlock_irqrestore(&adc->lock, flags);
}

static void jz_adc_irq_mask(unsigned int irq)
{
	struct jz_adc *adc = get_irq_chip_data(irq);
	irq = irq - adc->irq_base;

	jz_adc_irq_ctl(adc, irq, 1);
}

static void jz_adc_irq_unmask(unsigned int irq)
{
	struct jz_adc *adc = get_irq_chip_data(irq);
	irq = irq - adc->irq_base;

	jz_adc_irq_ctl(adc, irq, 0);
}

static void jz_adc_irq_ack(unsigned int irq)
{
	struct jz_adc *adc = get_irq_chip_data(irq);
	irq = irq - adc->irq_base;

	writeb(BIT(irq), adc->io_base + ADSTATE_OFF);
}

static unsigned int jz_adc_irq_startup(unsigned int irq)
{
	jz_adc_irq_unmask(irq);
	return 0;
}

static struct irq_chip jz_adc_irq_chip = {
	.name = "jz4775-adc",
	.startup = jz_adc_irq_startup,
	.shutdown = jz_adc_irq_mask,
	.mask = jz_adc_irq_mask,
	.unmask = jz_adc_irq_unmask,
	.ack = jz_adc_irq_ack,
};

static void jz_adc_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	struct jz_adc *adc = (struct jz_adc *)get_irq_data(irq);
	uint8_t status, mask, pending;

	status = readb(adc->io_base + ADSTATE_OFF);
	mask = readb(adc->io_base + ADCTRL_OFF);
	pending = status & ~mask;

	dev_dbg(&adc->pdev->dev, "%s: status = 0x%x, mask = 0x%x, pending = 0x%x\n",
			__func__, status, mask, pending);

	if (pending)
		generic_handle_irq(adc->irq_base + ffs(status) - 1);
}

static void jz_adc_setup_irq(struct jz_adc *adc)
{
	int irq;

	for (irq = adc->irq_base; irq < adc->irq_base + NUM_SADC; irq++) {
		set_irq_chip_and_handler(irq, &jz_adc_irq_chip, handle_level_irq);
		set_irq_chip_data(irq, adc);
	}

	set_irq_data(adc->irq, adc);
	set_irq_chained_handler(adc->irq, jz_adc_irq_demux);
}

static void jz_adc_remove_irq(struct jz_adc *adc)
{
	int irq;

	for (irq = adc->irq_base; irq < adc->irq_base + NUM_SADC; irq++) {
		set_irq_chip_and_handler(irq, &jz_adc_irq_chip, NULL);
		set_irq_chip_data(irq, NULL);
	}

	set_irq_data(adc->irq, NULL);
	set_irq_chained_handler(adc->irq, NULL);
}


static unsigned int get_sadcin(struct jz_adc *adc)
{
	unsigned int v;
	spinlock_t lock;
	int timeout = 0xfff;
	unsigned long flags;
	int re_enable = 0;

	spin_lock_init(&lock);
	spin_lock_irqsave(lock, flags);

	jz_adc_clk_enable(adc);
	/* for nested */
	re_enable = !!(readb(adc->io_base + ADENA_OFF) & ADENA_SADCINEN);
	re_enable |= !!(readb(adc->io_base + ADSTATE_OFF) & ADSTATE_SRDY);
	if (!re_enable) {
		jz_adc_dev_ctl(adc, 0, 1);
	}

	while (!(readb(adc->io_base + ADSTATE_OFF) & ADSTATE_SRDY) && timeout--) ;
	if (timeout <= 0)
		dev_warn(&adc->pdev->dev, "%s: wait SRDY timeout?\n", __func__);
	v = readl(adc->io_base + ADSDAT_OFF) & 0xfff;

	writeb(ADSTATE_SRDY, adc->io_base + ADSTATE_OFF);
	if (re_enable)
		jz_adc_dev_ctl(adc, 0, 1);
	jz_adc_clk_disable(adc);

	spin_unlock_irqrestore(lock, flags);

	return v * 3300 / 4096;
}

static ssize_t sadcin_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct jz_adc *adc = dev_get_drvdata(dev);

	dev_info(dev, "=== SADCIN: %d ===\n", get_sadcin(adc));

	return 0;
}

static const DEVICE_ATTR(sadcin, 0644,
						 sadcin_show, NULL);

static const struct attribute *debug_attrs[] = {
	/* &dev_attr_pbat.attr, */
	&dev_attr_sadcin.attr,
};

static const struct attribute_group debug_attr_group = {
	.attrs = (struct attribute **)debug_attrs,
};

static int sadcin_enable(struct platform_device *pdev)
{
	struct jz_adc *adc = dev_get_drvdata(pdev->dev.parent);

	jz_adc_clk_enable(adc);
	jz_adc_dev_ctl(adc, 0, 1);

	return 0;
}

static int sadcin_disable(struct platform_device *pdev)
{
	struct jz_adc *adc = dev_get_drvdata(pdev->dev.parent);

	jz_adc_dev_ctl(adc, 0, 0);
	jz_adc_clk_disable(adc);

	return 0;
}

static struct resource sadcin_resources[] = {
	{
		.start = 0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start = ADSDAT_OFF,
		.end   = ADSDAT_OFF + 3,
		.flags = IORESOURCE_MEM,
	},
};

static struct mfd_cell jz_adc_cells[] = {
	{
		.name = "sadcin",
		.num_resources = ARRAY_SIZE(sadcin_resources),
		.resources = sadcin_resources,

		.enable = sadcin_enable,
		.disable = sadcin_disable,
	}
};

static int __devinit jz_adc_probe(struct platform_device *pdev)
{
	int ret, i;
	struct resource *res;
	struct jz_adc *adc;
	struct jz_adc_platform_data *adc_pdata;

	adc = kzalloc(sizeof(*adc), GFP_KERNEL);
	if (adc == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	adc->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto err_free;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to request memory resource\n");
		ret = -EBUSY;
		goto err_free;
	}
	adc->mem = res;

	adc->io_base = ioremap_nocache(res->start, resource_size(res));
	if (adc->io_base == NULL) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		ret = -ENODEV;
		goto err_free_mem;
	}

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		ret = -ENODEV;
		goto err_free_io;
	}
	adc->irq_base = platform_get_irq(pdev, 1);
	if (adc->irq_base < 0) {
		dev_err(&pdev->dev, "no IRQ Base resource defined\n");
		ret = -ENODEV;
		goto err_free_io;
	}

	spin_lock_init(&adc->lock);
	atomic_set(&adc->clk_ref, 0);

	platform_set_drvdata(pdev, adc);

	ret = sysfs_create_group(&pdev->dev.kobj, &debug_attr_group);
	if (ret) {
		dev_warn(&pdev->dev, "failed to register sysfs hooks\n");
	}

	adc_pdata = pdev->dev.platform_data;
	if (adc_pdata) {
		for (i = 0; i < ARRAY_SIZE(jz_adc_cells); i++) {
			struct adc_cell_platform_data *pdata = adc_pdata->cell_pdata[i];
			if (pdata && !strcmp(pdata->name, jz_adc_cells[i].name)) {
				pdata->cell = &jz_adc_cells[i];
				jz_adc_cells[i].platform_data = pdata;
				jz_adc_cells[i].data_size = sizeof(struct adc_cell_platform_data);
			} else {
				dev_warn(&pdev->dev, "miss cells[%d] platform_data !\n", i);
			}
		}
	}
	ret = mfd_add_devices(&pdev->dev, 0, jz_adc_cells, ARRAY_SIZE(jz_adc_cells),
						  adc->mem, adc->irq_base);

	/* disable controller */
	jz_adc_clk_enable(adc);
	writeb(0, adc->io_base + ADENA_OFF);
	writeb(0xff, adc->io_base + ADCTRL_OFF);
	writeb(0xff, adc->io_base + ADSTATE_OFF);
	jz_adc_clk_disable(adc);

	jz_adc_setup_irq(adc);

	return 0;

err_free_io:
	iounmap(adc->io_base);
err_free_mem:
	release_mem_region(adc->mem->start, resource_size(adc->mem));
err_free:
	kfree(adc);
	return ret;
}

static int __devexit jz_adc_remove(struct platform_device *pdev)
{
	struct jz_adc *adc = platform_get_drvdata(pdev);

	jz_adc_clk_disable(adc);
	jz_adc_remove_irq(adc);
	mfd_remove_devices(&pdev->dev);
	iounmap(adc->io_base);
	release_mem_region(adc->mem->start, resource_size(adc->mem));
	platform_set_drvdata(pdev, NULL);
	kfree(adc);

	return 0;
}

static struct platform_driver jz_adc_driver = {
	.probe  = jz_adc_probe,
	.remove = __devexit_p(jz_adc_remove),
	.driver = {
		.name   = "jz4750l-adc",
		.owner  = THIS_MODULE,
	},
};

static int __init jz_adc_init(void)
{
	return platform_driver_register(&jz_adc_driver);
}
module_init(jz_adc_init);

static void __exit jz_adc_exit(void)
{
	platform_driver_unregister(&jz_adc_driver);
}
module_exit(jz_adc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jiang Tao<tao.jiang@ingneic.com");
MODULE_DESCRIPTION("JZ SOC ADC driver");
