/*
 * linux/drivers/input/keyboard/jz_gpio_keys.c
 *
 * Keypad driver based on GPIO pins for Jz4750 APUS board.
 *
 * User applications can access to this device via /dev/input/eventX.
 *
 * Copyright (c) 2005 - 2009  Ingenic Semiconductor Inc.
 *
 * Author: Richard <cjfeng@ingenic.cn>
 *         Regen   <lhhuang@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <asm/gpio.h>
#include <asm/jzsoc.h>

#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define dprintk(x...)	printk(x)
#else
#define dprintk(x...)
#endif

/* Device name */
#if defined(CONFIG_JZ4750_APUS)
#define DEV_NAME "apus-keypad"
#elif defined(CONFIG_JZ4750D_CETUS)
#define DEV_NAME "cetus-keypad"
#else
#define DEV_NAME "jz-keypad"
#endif

/* Timer interval */
#define SCAN_INTERVAL	5
#define KEY_NUM		ARRAY_SIZE(board_buttons)

//#define KEY_FOR_MPLAYER
//#define KEY_FOR_CUR_TEST
//#define KEY_FOR_RECOVERY
#define KEY_FOR_CARINA
/*
 * GPIO Buttons,
 * .code conforms with android/build/target/board/<boardname>/<boardname>-keypad.kl
 */

#ifdef KEY_FOR_CARINA
 static struct gpio_keys_button board_buttons[] = {
#ifdef GPIO_WPS
	{
		.gpio		= GPIO_WPS,
		.code   	= KEY_SEND,
		.desc		= "WIFI WPS",
		.active_low	= ACTIVE_LOW_WPS,
	},
#endif
#ifdef GPIO_PAUSE
	{
		.gpio		= GPIO_PAUSE,
		.code   	= KEY_HOME,
		.desc		= "PAUSE KEY",
		.active_low	= ACTIVE_LOW_PAUSE,
	},
#endif
};
#endif //endof KEY_FOR_CARINA


#ifdef KEY_FOR_RECOVERY
 static struct gpio_keys_button board_buttons[] = {
#ifdef GPIO_CALL
	{
		.gpio		= GPIO_CALL,
		.code   	= KEY_SEND,
		.desc		= "call key",
		.active_low	= ACTIVE_LOW_CALL,
	},
#endif
#ifdef GPIO_HOME
	{
		.gpio		= GPIO_HOME,
		.code   	= KEY_HOME,
		.desc		= "home key",
		.active_low	= ACTIVE_LOW_HOME,
	},
#endif
#ifdef GPIO_BACK
	{
		.gpio		= GPIO_BACK,
		.code   	= KEY_BACK,
		.desc		= "back key",
		.active_low	= ACTIVE_LOW_BACK,
	},
#endif
#ifdef GPIO_MENU
	{
		.gpio		= GPIO_MENU,
		.code   	= KEY_MENU,
		.desc		= "menu key",
		.active_low	= ACTIVE_LOW_MENU,
	},
#endif
#ifdef GPIO_ENDCALL
	{
		.gpio		= GPIO_ENDCALL,
		.code   	= KEY_END,
		.desc		= "end call key",
		.active_low	= ACTIVE_LOW_ENDCALL,
	},
#endif
#ifdef GPIO_VOLUMEDOWN
	{
		.gpio		= GPIO_VOLUMEDOWN,
		.code   	= KEY_VOLUMEDOWN,
		.desc		= "volum down key",
		.active_low	= ACTIVE_LOW_VOLUMEDOWN,
	},
	{
		.gpio		= GPIO_VOLUMEUP,
		.code   	= KEY_VOLUMEUP,
		.desc		= "volum up key",
		.active_low	= ACTIVE_LOW_VOLUMEUP,
	},
#endif
};
#endif

#ifdef KEY_FOR_MPLAYER
static struct gpio_keys_button board_buttons[] = {
	{
		.gpio		= GPIO_MP_VOLUMEUP,
		.code   	= KEY_VOLUMEUP,
		.desc		= "volume up key",
		.active_low	= ACTIVE_LOW_VOLUMEUP,
	},
	{
		.gpio		= GPIO_MP_VOLUMEDOWN,
		.code   	= KEY_VOLUMEDOWN,
		.desc		= "volume down key",
		.active_low	= ACTIVE_LOW_VOLUMEDOWN,
	},
	{
		.gpio		= GPIO_MP_MUTE,
		.code   	= KEY_MUTE,
		.desc		= "mute key",
		.active_low	= ACTIVE_LOW_MUTE,
	},
	{
		.gpio		= GPIO_MP_PAUSE,
		.code   	= KEY_PAUSE,
		.desc		= "pause key",
		.active_low	= ACTIVE_LOW_PUASE,
	},
	{
		.gpio		= GPIO_MP_PLAY,
		.code   	= KEY_PLAY,
		.desc		= "play key",
		.active_low	= ACTIVE_LOW_PLAY,
	},
	{
		.gpio		= GPIO_MP_REWIND,
		.code   	= KEY_REWIND,
		.desc		= "rewind key",
		.active_low	= ACTIVE_LOW_REWIND,
	},
	{
		.gpio		= GPIO_MP_FORWARD,
		.code   	= KEY_FORWARD,
		.desc		= "volum up key",
		.active_low	= ACTIVE_LOW_FORWARD,
	},
};
#endif	/* KEY_FOR_MPLAYER */

#ifdef KEY_FOR_CUR_TEST
static struct gpio_keys_button board_buttons[] = {
	{
		.gpio		= GPIO_SW3,
		.code   	= KEY_F1,
		.desc		= "mp4 test",
		.active_low	= ACTIVE_LOW_SW3,
	},
	{
		.gpio		= GPIO_SW1,
		.code   	= KEY_F2,
		.desc		= "gcc test",
		.active_low	= ACTIVE_LOW_SW1,
	},
	{
		.gpio		= GPIO_SW7,
		.code   	= KEY_F3,
		.desc		= "suspend",
		.active_low	= ACTIVE_LOW_SW7,
	},
	{
		.gpio		= GPIO_SW8,
		.code   	= KEY_F4,
		.desc		= "hibernation",
		.active_low	= ACTIVE_LOW_SW8,
	},
	{
		.gpio		= GPIO_SW6,
		.code   	= KEY_F5,
		.desc		= "idle",
		.active_low	= ACTIVE_LOW_SW6,
	},
};
#endif	/* KEY_FOR_CUR_TEST */

static struct timer_list kbd_timer[KEY_NUM];
static int current_key[KEY_NUM];
struct semaphore sem;

static struct gpio_keys_platform_data board_button_data = {
	.buttons	= board_buttons,
	.nbuttons	= KEY_NUM,
};

static struct platform_device board_button_device = {
	.name		= DEV_NAME,
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &board_button_data,
	}
};

static void enable_gpio_irqs(struct gpio_keys_platform_data *pdata)
{
	int i;

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];

		if (button->active_low)
			__gpio_as_irq_fall_edge(button->gpio);
		else
			__gpio_as_irq_rise_edge(button->gpio);
	}
}

static void button_timer_callback(unsigned long data)
{
	int gpio;
	int code;
	int active_low;
	struct platform_device *pdev = (struct platform_device *)data;
	struct input_dev *input = platform_get_drvdata(pdev);
	int state, i;
	static int button_pressed[KEY_NUM] = { 0,};

	down(&sem);

	for (i = 0; i < KEY_NUM; i++) {
		if (1 == current_key[i]) {
			gpio = board_buttons[i].gpio;
			code = board_buttons[i].code;
			active_low = board_buttons[i].active_low;

			state = __gpio_get_pin(gpio);

			if (active_low ^ state) {
				if(button_pressed[i] != 1) {
					/* button pressed */
					button_pressed[i] = 1;
					input_report_key(input, code, 1);
					input_sync(input);
					dprintk("gpio %d down, code:%d \n", gpio, code);
				}
				mod_timer(&kbd_timer[i],jiffies + SCAN_INTERVAL);
			} else {
				/* button released */
				if (1 == button_pressed[i]) {
					input_report_key(input, code, 0);
					input_sync(input);
					button_pressed[i] = 0;
					current_key[i] = 0;
					dprintk("gpio %d up, code:%d \n", gpio, code);
				}
			}
		}
	}
	up(&sem);

}

static irqreturn_t jz_gpio_interrupt(int irq, void *dev_id)
{
	int i;
	struct platform_device *pdev = dev_id;
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;

	dprintk("--irq of gpio:%d\n", irq - IRQ_GPIO_0);

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		int gpio = button->gpio;

		if (irq == (gpio + IRQ_GPIO_0) ) {
			current_key[i] = 1;
			/* start timer */
			mod_timer(&kbd_timer[i], jiffies + SCAN_INTERVAL);
			dprintk("--mod_timer for gpio:%d\n", gpio);
			break;
		}
	}

	return IRQ_HANDLED;
}

static int __devinit gpio_keys_probe(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct input_dev *input;
	int i, error;
	int wakeup = 0;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	init_MUTEX(&sem);

	platform_set_drvdata(pdev, input);

	input->name = pdev->name;
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	input->evbit[0] = BIT(EV_KEY) | BIT(EV_SYN);

	set_bit(KEY_MENU, input->keybit);
	set_bit(KEY_HOME, input->keybit);
	set_bit(KEY_SEND, input->keybit);
	set_bit(KEY_BACK, input->keybit);
	set_bit(KEY_END, input->keybit);
	set_bit(KEY_VOLUMEDOWN, input->keybit);
	set_bit(KEY_VOLUMEUP, input->keybit);

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		int irq;
		unsigned int type = button->type ?: EV_KEY;

		/* Init timer */
		setup_timer(&kbd_timer[i],
			    button_timer_callback,
			    (unsigned long)&board_button_device);

		irq = IRQ_GPIO_0 + button->gpio;
		if (irq < 0) {
			error = irq;
			pr_err("%s: Unable to get irq number"
			       " for GPIO %d, error %d\n", DEV_NAME,
				button->gpio, error);
			goto fail;
		}

		error = request_irq(irq, jz_gpio_interrupt,
				    IRQF_SAMPLE_RANDOM | IRQF_DISABLED,
				    button->desc ? button->desc : "gpio_keys",
				    pdev);
		if (error) {
			pr_err("%s: Unable to claim irq %d; error %d\n",
			       DEV_NAME, irq, error);
			goto fail;
		}

		if (button->wakeup)
			wakeup = 1;

		input_set_capability(input, type, button->code);
	}

	/* Enable all GPIO irqs */
	enable_gpio_irqs(pdata);

	error = input_register_device(input);
	if (error) {
		pr_err("%s: Unable to register input device, "
		       "error: %d\n", DEV_NAME, error);
		goto fail;
	}

	device_init_wakeup(&pdev->dev, wakeup);

	return 0;

 fail:
	while (--i >= 0) {
		free_irq(pdata->buttons[i].gpio + IRQ_GPIO_0 , pdev);
	}

	platform_set_drvdata(pdev, NULL);
	input_free_device(input);

	return error;
}

static int __devexit gpio_keys_remove(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct input_dev *input = platform_get_drvdata(pdev);
	int i;

	device_init_wakeup(&pdev->dev, 0);

	for (i = 0; i < pdata->nbuttons; i++) {
		int irq = pdata->buttons[i].gpio + IRQ_GPIO_0;
		free_irq(irq, pdev);
	}

	input_unregister_device(input);

	return 0;
}

#ifdef CONFIG_PM

static int gpio_keys_suspend(struct platform_device *pdev, pm_message_t state)
{
#if 0
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	int i;

	if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct gpio_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq = button->gpio + IRQ_GPIO_0;
				enable_irq_wake(irq);
			}
		}
	}
#endif
	return 0;
}

static int gpio_keys_resume(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
#if 0
	int i;

	if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct gpio_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq = button->gpio + IRQ_GPIO_0;
				disable_irq_wake(irq);
			}
		}
	}
#endif

	/* Enable all GPIO irqs */
	enable_gpio_irqs(pdata);

	return 0;
}
#else
#define gpio_keys_suspend	NULL
#define gpio_keys_resume	NULL
#endif

static struct platform_driver gpio_keys_device_driver = {
	.probe		= gpio_keys_probe,
	.remove		= __devexit_p(gpio_keys_remove),
	.suspend	= gpio_keys_suspend,
	.resume		= gpio_keys_resume,
	.driver		= {
		.name	= DEV_NAME,
	}
};

static int __init gpio_keys_init(void)
{
	int ret;

	printk("=======>gpio_keys_init!!!\n");
	platform_device_register(&board_button_device);
	ret = platform_driver_register(&gpio_keys_device_driver);

	return ret;
}

static void __exit gpio_keys_exit(void)
{
	platform_device_unregister(&board_button_device);
	platform_driver_unregister(&gpio_keys_device_driver);
}

module_init(gpio_keys_init);
module_exit(gpio_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Regen Huang <lhhuang@ingenic.cn>");
MODULE_DESCRIPTION("Keyboard driver for CPU GPIOs");
