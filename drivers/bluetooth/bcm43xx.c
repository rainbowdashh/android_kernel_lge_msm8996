/*
 * Copyright (C) 2009-2011 Google, Inc.
 * Copyright (C) 2009-2014 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/fs.h>

#ifndef __LPM_BLUESLEEP__
#define __LPM_BLUESLEEP__   1
#endif

extern void bt_export_bd_address(void);
extern void msm_hs_uart_gpio_config_ext(int on);
#ifdef __LPM_BLUESLEEP__
extern void bluesleep_set_bt_pwr_state(int on);
#endif

/* BT chip power up and wakeup pins (these define not used)*/
#define BT_REG_ON    73
#define BT_WAKE_HOST 117
#define BT_WAKE_DEV  118

/* UART pins (these define not used)*/
#define BT_UART_RTSz  44
#define BT_UART_CTSz  43
#define BT_UART_RX    42
#define BT_UART_TX    41

static struct rfkill *bt_rfk;
static const char bt_name[] = "bcm43xx";

/* BT GPIO pins variable */
static int gpio_bt_reg_on;

/* BT_WAKE_HOST pin control */
struct pinctrl *bt_pinctrl;
struct pinctrl_state *bt_wake_host_set_state_on;
struct pinctrl_state *bt_wake_host_set_state_off;


static void bcm43xx_config_bt_on(void)
{
	int rc = 0;
	printk(KERN_INFO "[BT] == R ON ==\n");

	if (gpio_bt_reg_on < 0) {
		printk(KERN_INFO "[BT] bt_reg_on:%d !!\n", gpio_bt_reg_on);
	}

	/* Config UART pins */
	msm_hs_uart_gpio_config_ext(1);

	/* Host wake setup to I(PU) per data sheet */
	rc = pinctrl_select_state(bt_pinctrl, bt_wake_host_set_state_on);
	if (rc) printk("[BT] cannot set BT pinctrl gpio state on: %d\n", rc);

	/* Power up BT controller */
	rc = gpio_direction_output(gpio_bt_reg_on, 0);
	if (rc) printk(KERN_INFO "[BT] set REG_ON 0 fail: %d\n", rc);
	mdelay(5);
	rc = gpio_direction_output(gpio_bt_reg_on, 1);
	if (rc) printk(KERN_INFO "[BT] set REG_ON 1 fail: %d\n", rc);
	mdelay(5);

#ifdef __LPM_BLUESLEEP__
	/* Notify sleep driver BT state */
	bluesleep_set_bt_pwr_state(1);
#endif
}

static void bcm43xx_config_bt_off(void)
{
	int rc = 0;

	if (gpio_bt_reg_on < 0) {
		printk(KERN_INFO "[BT] bt_reg_on:%d !!\n", gpio_bt_reg_on);
	}

#ifdef __LPM_BLUESLEEP__
	/* Notify sleep driver BT state */
	bluesleep_set_bt_pwr_state(0);
#endif

	/* Power off BT controller */
	rc = gpio_direction_output(gpio_bt_reg_on, 0);
	if (rc) printk(KERN_INFO "[BT] set REG_ON 0 fail: %d\n", rc);

	/* Host wake setup to I(PD) per GPIO table */
	rc = pinctrl_select_state(bt_pinctrl, bt_wake_host_set_state_off);
	if (rc) printk("[BT] cannot set BT pinctrl gpio state off: %d\n", rc);

	mdelay(2);

	/* Config UART pins */
	msm_hs_uart_gpio_config_ext(0);

	printk(KERN_INFO "[BT] == R OFF ==\n");
}

static int bluetooth_set_power(void *data, bool blocked)
{
	if (!blocked) {
		bcm43xx_config_bt_on();
	} else
		bcm43xx_config_bt_off();

	return 0;
}

static struct rfkill_ops bcm43xx_rfkill_ops = {
	.set_block = bluetooth_set_power,
};

static int bcm43xx_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	bool default_state = true;  /* off */
	struct pinctrl_state *set_state;

	printk(KERN_INFO "[BT] == rfkill_probe ==\n");

	gpio_bt_reg_on = of_get_named_gpio(pdev->dev.of_node,
					 "bcm,bt-regon-gpio", 0);
	if (gpio_bt_reg_on < 0)
		return gpio_bt_reg_on;

	bt_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(bt_pinctrl))
		return PTR_ERR(bt_pinctrl);
	set_state = pinctrl_lookup_state(bt_pinctrl, "bt_wake_host_gpio_on");
	if (IS_ERR(set_state))
		return PTR_ERR(set_state);
	bt_wake_host_set_state_on = set_state;
	set_state = pinctrl_lookup_state(bt_pinctrl, "bt_wake_host_gpio_off");
	if (IS_ERR(set_state))
		return PTR_ERR(set_state);
	bt_wake_host_set_state_off = set_state;

	rc = devm_gpio_request_one(&pdev->dev, gpio_bt_reg_on,
				   GPIOF_OUT_INIT_LOW, "BRCM_BT_EN");
	if (rc)
		return rc;

	bluetooth_set_power(NULL, default_state);

	bt_rfk = rfkill_alloc(bt_name, &pdev->dev, RFKILL_TYPE_BLUETOOTH,
				&bcm43xx_rfkill_ops, NULL);
	if (!bt_rfk) {
		rc = -ENOMEM;
		goto err_rfkill_alloc;
	}

	rfkill_set_states(bt_rfk, default_state, false);

	/* userspace cannot take exclusive control */

	rc = rfkill_register(bt_rfk);
	if (rc)
		goto err_rfkill_reg;

	return 0;

err_rfkill_reg:
	rfkill_destroy(bt_rfk);
err_rfkill_alloc:
	return rc;
}

static int bcm43xx_rfkill_remove(struct platform_device *dev)
{

	bluetooth_set_power(NULL, true);
	rfkill_unregister(bt_rfk);
	rfkill_destroy(bt_rfk);

	return 0;
}

static const struct of_device_id bcm43xx_rfkill_match_table[] = {
	{ .compatible = "bcm,bcm43xx", },
	{},
};

static struct platform_driver bcm43xx_rfkill_driver = {
	.probe = bcm43xx_rfkill_probe,
	.remove = bcm43xx_rfkill_remove,
	.driver = {
		.name = "bcm43xx",
		.owner = THIS_MODULE,
		.of_match_table = bcm43xx_rfkill_match_table,
	},
};

static int __init bcm43xx_rfkill_init(void)
{
	return platform_driver_register(&bcm43xx_rfkill_driver);
}

static void __exit bcm43xx_rfkill_exit(void)
{
	platform_driver_unregister(&bcm43xx_rfkill_driver);
}

module_init(bcm43xx_rfkill_init);
module_exit(bcm43xx_rfkill_exit);
MODULE_ALIAS("platform:bcm43xx");
MODULE_DESCRIPTION("bcm43xx_rfkill");
MODULE_AUTHOR("Jaikumar Ganesh <jaikumar@google.com>");
MODULE_LICENSE("GPL v2");
