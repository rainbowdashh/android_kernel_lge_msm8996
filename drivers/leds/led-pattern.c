// SPDX-License-Identifier: GPL-2.0-only
/* LG LED pattern interface, adapted from the MSM8996 Linux 4.4 driver. */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <soc/qcom/lge/board_lge.h>
#include "led-pattern.h"

static DEFINE_MUTEX(pattern_lock);
static struct class *pattern_class;
static struct device *pattern_device;
static struct led_pattern_ops *pattern_ops;

enum pattern_operation {
	PATTERN_SELECT,
	PATTERN_INPUT,
	PATTERN_BLINK,
	PATTERN_ONOFF,
	PATTERN_SCALE,
};

static ssize_t pattern_store(enum pattern_operation op, const char *buf,
			     size_t count)
{
	ssize_t (*store)(const char *, size_t) = NULL;
	ssize_t ret;

	mutex_lock(&pattern_lock);
	if (!pattern_ops) {
		ret = -ENODEV;
		goto out;
	}
	switch (op) {
	case PATTERN_SELECT:
		/* Preserve the factory-boot restriction of the LG interface. */
		if (lge_get_boot_mode() > LGE_BOOT_MODE_CHARGERLOGO) {
			ret = count;
			goto out;
		}
		store = pattern_ops->select;
		break;
	case PATTERN_INPUT:
		store = pattern_ops->input;
		break;
	case PATTERN_BLINK:
		store = pattern_ops->blink;
		break;
	case PATTERN_ONOFF:
		store = pattern_ops->onoff;
		break;
	case PATTERN_SCALE:
		store = pattern_ops->scale;
		break;
	}
	ret = store ? store(buf, count) : -EOPNOTSUPP;
out:
	mutex_unlock(&pattern_lock);
	return ret;
}

#define PATTERN_ATTR(name, op) \
static ssize_t name##_store(struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{ \
	return pattern_store(op, buf, count); \
} \
static DEVICE_ATTR(name, 0200, NULL, name##_store)

PATTERN_ATTR(setting, PATTERN_SELECT);
PATTERN_ATTR(input_patterns, PATTERN_INPUT);
PATTERN_ATTR(blink_patterns, PATTERN_BLINK);
PATTERN_ATTR(onoff_patterns, PATTERN_ONOFF);
PATTERN_ATTR(scale, PATTERN_SCALE);

static struct attribute *pattern_attrs[] = {
	&dev_attr_setting.attr,
	&dev_attr_input_patterns.attr,
	&dev_attr_blink_patterns.attr,
	&dev_attr_onoff_patterns.attr,
	&dev_attr_scale.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pattern);

/* Publish attributes only after the hardware backend has finished probing. */
int led_pattern_register(struct led_pattern_ops *ops)
{
	int ret = 0;

	mutex_lock(&pattern_lock);
	if (pattern_ops) {
		ret = -EBUSY;
		goto out;
	}
	pattern_class = class_create(THIS_MODULE, "lg_rgb_led");
	if (IS_ERR(pattern_class)) {
		ret = PTR_ERR(pattern_class);
		goto out;
	}
	pattern_device = device_create_with_groups(pattern_class, NULL, 0, NULL,
						  pattern_groups, "use_patterns");
	if (IS_ERR(pattern_device)) {
		ret = PTR_ERR(pattern_device);
		class_destroy(pattern_class);
		goto out;
	}
	pattern_ops = ops;
out:
	mutex_unlock(&pattern_lock);
	return ret;
}

void led_pattern_unregister(void)
{
	mutex_lock(&pattern_lock);
	if (!pattern_ops) {
		mutex_unlock(&pattern_lock);
		return;
	}
	pattern_ops = NULL;
	mutex_unlock(&pattern_lock);

	device_unregister(pattern_device);
	class_destroy(pattern_class);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LG LED pattern class interface");
