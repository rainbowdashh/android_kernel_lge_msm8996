// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/leds-qpnp-flash.h>
#include <linux/mutex.h>

#include "leds.h"

struct flash_data {
	struct list_head link;
	struct device *dev;
	int (*func)(struct led_trigger *trig, int options,
					int *max_current);
};

static LIST_HEAD(flash_common_data);
static DEFINE_MUTEX(flash_common_lock);

static void qpnp_flash_remove_prepare(void *data)
{
	struct flash_data *flash_data = data;

	mutex_lock(&flash_common_lock);
	list_del_init(&flash_data->link);
	mutex_unlock(&flash_common_lock);
}

/* Stop callbacks before the provider tears down its LEDs and regulators. */
void qpnp_flash_unregister_led_prepare(struct device *dev)
{
	struct flash_data *flash_data, *next;

	mutex_lock(&flash_common_lock);
	list_for_each_entry_safe(flash_data, next, &flash_common_data, link) {
		if (flash_data->dev == dev)
			list_del_init(&flash_data->link);
	}
	mutex_unlock(&flash_common_lock);
}

int qpnp_flash_register_led_prepare(struct device *dev, void *data)
{
	struct flash_data *flash_data;

	flash_data = devm_kzalloc(dev, sizeof(struct flash_data),
						GFP_KERNEL);
	if (!flash_data)
		return -ENOMEM;

	flash_data->dev = dev;
	flash_data->func = data;

	mutex_lock(&flash_common_lock);
	list_add(&flash_data->link, &flash_common_data);
	mutex_unlock(&flash_common_lock);

	return devm_add_action_or_reset(dev, qpnp_flash_remove_prepare, flash_data);
}

int qpnp_flash_led_prepare(struct led_trigger *trig, int options,
					int *max_current)
{
	struct flash_data *flash_data;
	struct led_classdev *led_cdev;
	int rc = -ENODEV;

	mutex_lock(&flash_common_lock);
	led_cdev = trigger_to_lcdev(trig);
	if (!led_cdev)
		goto out;

	list_for_each_entry(flash_data, &flash_common_data, link) {
		if (led_cdev->dev->parent == flash_data->dev) {
			rc = flash_data->func(trig, options, max_current);
			break;
		}
	}

out:
	mutex_unlock(&flash_common_lock);
	return rc;
}
