// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm legacy thermal threshold arbitration, ported from Linux 4.4.
 * Copyright (c) 2008, 2010 Intel Corporation
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/thermal.h>
#include <linux/slab.h>
#include "thermal_core.h"

static LIST_HEAD(sensor_info_list);
static DEFINE_MUTEX(sensor_list_lock);

static struct sensor_info *get_sensor(uint32_t sensor_id)
{
	struct sensor_info *pos = NULL, *matching_sensor = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &sensor_info_list, sensor_list) {
		if (pos->sensor_id == sensor_id) {
			matching_sensor = pos;
			break;
		}
	}
	rcu_read_unlock();

	return matching_sensor;
}

int sensor_get_id(char *name)
{
	struct sensor_info *pos = NULL;
	int matching_id = -ENODEV;

	if (!name)
		return matching_id;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &sensor_info_list, sensor_list) {
		if (!strcmp(pos->tz->type, name)) {
			matching_id = pos->sensor_id;
			break;
		}
	}
	rcu_read_unlock();

	return matching_id;
}
EXPORT_SYMBOL(sensor_get_id);

static void init_sensor_trip(struct sensor_info *sensor)
{
	int ret = 0, i = 0;
	enum thermal_trip_type type;

	for (i = 0; ((sensor->max_idx == -1) ||
		(sensor->min_idx == -1)) &&
		(sensor->tz->ops->get_trip_type) &&
		(i < sensor->tz->trips); i++) {

		sensor->tz->ops->get_trip_type(sensor->tz, i, &type);
		if (type == THERMAL_TRIP_CONFIGURABLE_HI)
			sensor->max_idx = i;
		if (type == THERMAL_TRIP_CONFIGURABLE_LOW)
			sensor->min_idx = i;
		type = 0;
	}

	if (sensor->max_idx < 0 || sensor->min_idx < 0)
		return;

	ret = sensor->tz->ops->get_trip_temp(sensor->tz,
		sensor->min_idx, &sensor->threshold_min);
	if (ret)
		pr_err("Unable to get MIN trip temp. sensor:%d err:%d\n",
				sensor->sensor_id, ret);

	ret = sensor->tz->ops->get_trip_temp(sensor->tz,
		sensor->max_idx, &sensor->threshold_max);
	if (ret)
		pr_err("Unable to get MAX trip temp. sensor:%d err:%d\n",
				sensor->sensor_id, ret);
}

static int __update_sensor_thresholds(struct sensor_info *sensor)
{
	long max_of_low_thresh = LONG_MIN;
	long min_of_high_thresh = LONG_MAX;
	struct sensor_threshold *pos = NULL;
	int ret = 0;

	if (!sensor->tz->ops->set_trip_temp ||
		!sensor->tz->ops->activate_trip_type ||
		!sensor->tz->ops->get_trip_type ||
		!sensor->tz->ops->get_trip_temp) {
		ret = -ENODEV;
		goto update_done;
	}

	if ((sensor->max_idx == -1) || (sensor->min_idx == -1))
		init_sensor_trip(sensor);
	if (sensor->max_idx < 0 || sensor->min_idx < 0)
		return -ENODEV;

	list_for_each_entry(pos, &sensor->threshold_list, list) {
		if (!pos->active)
			continue;
		if (pos->trip == THERMAL_TRIP_CONFIGURABLE_LOW) {
			if (pos->temp > max_of_low_thresh)
				max_of_low_thresh = pos->temp;
		}
		if (pos->trip == THERMAL_TRIP_CONFIGURABLE_HI) {
			if (pos->temp < min_of_high_thresh)
				min_of_high_thresh = pos->temp;
		}
	}

	pr_debug("sensor %d: Thresholds: max of low: %ld min of high: %ld\n",
			sensor->sensor_id, max_of_low_thresh,
			min_of_high_thresh);

	if (min_of_high_thresh != LONG_MAX) {
		ret = sensor->tz->ops->set_trip_temp(sensor->tz,
			sensor->max_idx, min_of_high_thresh);
		if (ret) {
			pr_err("sensor %d: Unable to set high threshold %d",
					sensor->sensor_id, ret);
			goto update_done;
		}
		sensor->threshold_max = min_of_high_thresh;
	}
	ret = sensor->tz->ops->activate_trip_type(sensor->tz,
		sensor->max_idx,
		(min_of_high_thresh == LONG_MAX) ?
		THERMAL_TRIP_ACTIVATION_DISABLED :
		THERMAL_TRIP_ACTIVATION_ENABLED);
	if (ret) {
		pr_err("sensor %d: Unable to activate high threshold %d",
			sensor->sensor_id, ret);
		goto update_done;
	}

	if (max_of_low_thresh != LONG_MIN) {
		ret = sensor->tz->ops->set_trip_temp(sensor->tz,
			sensor->min_idx, max_of_low_thresh);
		if (ret) {
			pr_err("sensor %d: Unable to set low threshold %d",
				sensor->sensor_id, ret);
			goto update_done;
		}
		sensor->threshold_min = max_of_low_thresh;
	}
	ret = sensor->tz->ops->activate_trip_type(sensor->tz,
		sensor->min_idx,
		(max_of_low_thresh == LONG_MIN) ?
		THERMAL_TRIP_ACTIVATION_DISABLED :
		THERMAL_TRIP_ACTIVATION_ENABLED);
	if (ret) {
		pr_err("sensor %d: Unable to activate low threshold %d",
			sensor->sensor_id, ret);
		goto update_done;
	}

	pr_debug("sensor %d: low: %d high: %d\n",
		sensor->sensor_id,
		sensor->threshold_min, sensor->threshold_max);

update_done:
	return ret;
}

static void sensor_update_work(struct work_struct *work)
{
	struct sensor_info *sensor = container_of(work, struct sensor_info,
						work);
	int ret = 0;
	sysfs_notify(&sensor->tz->device.kobj, NULL, "type");
	mutex_lock(&sensor->lock);
	ret = __update_sensor_thresholds(sensor);
	if (ret)
		pr_err("sensor %d: Error %d setting threshold\n",
			sensor->sensor_id, ret);
	mutex_unlock(&sensor->lock);
}

/* May be called in an interrupt context.
 * Do NOT call sensor_set_trip from this function
 */
int thermal_sensor_trip(struct thermal_zone_device *tz,
		enum thermal_trip_type trip, long temp)
{
	struct sensor_threshold *pos = NULL;
	int ret = -ENODEV;

	if (trip != THERMAL_TRIP_CONFIGURABLE_HI &&
			trip != THERMAL_TRIP_CONFIGURABLE_LOW)
		return 0;

	if (list_empty(&tz->sensor.threshold_list))
		return 0;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &tz->sensor.threshold_list, list) {
		if ((pos->trip != trip) || (!pos->active))
			continue;
		if (((trip == THERMAL_TRIP_CONFIGURABLE_LOW) &&
			(pos->temp <= tz->sensor.threshold_min) &&
			(pos->temp >= temp)) ||
			((trip == THERMAL_TRIP_CONFIGURABLE_HI) &&
				(pos->temp >= tz->sensor.threshold_max) &&
				(pos->temp <= temp))) {
			pos->active = 0;
			pos->notify(trip, temp, pos->data);
		}
	}
	rcu_read_unlock();

	schedule_work(&tz->sensor.work);

	return ret;
}
EXPORT_SYMBOL(thermal_sensor_trip);

int sensor_get_temp(uint32_t sensor_id, int *temp)
{
	struct sensor_info *sensor = get_sensor(sensor_id);
	int ret = 0;

	if (!sensor)
		return -ENODEV;

	ret = sensor->tz->ops->get_temp(sensor->tz, temp);

	return ret;
}
EXPORT_SYMBOL(sensor_get_temp);

int sensor_activate_trip(uint32_t sensor_id,
	struct sensor_threshold *threshold, bool enable)
{
	struct sensor_info *sensor = get_sensor(sensor_id);
	int ret = 0;

	if (!sensor || !threshold) {
		pr_err("%s: uninitialized data\n",
			KBUILD_MODNAME);
		ret = -ENODEV;
		goto activate_trip_exit;
	}

	mutex_lock(&sensor->lock);
	threshold->active = (enable) ? 1 : 0;
	ret = __update_sensor_thresholds(sensor);
	mutex_unlock(&sensor->lock);

activate_trip_exit:
	return ret;
}
EXPORT_SYMBOL(sensor_activate_trip);

int sensor_set_trip(uint32_t sensor_id, struct sensor_threshold *threshold)
{
	struct sensor_threshold *pos = NULL;
	struct sensor_info *sensor = get_sensor(sensor_id);

	if (!sensor)
		return -ENODEV;

	if (!threshold || !threshold->notify)
		return -EFAULT;

	mutex_lock(&sensor->lock);
	list_for_each_entry(pos, &sensor->threshold_list, list) {
		if (pos == threshold)
			break;
	}

	if (pos != threshold) {
		INIT_LIST_HEAD(&threshold->list);
		list_add_rcu(&threshold->list, &sensor->threshold_list);
	}
	threshold->active = 0; /* Do not allow active threshold right away */

	mutex_unlock(&sensor->lock);

	return 0;

}
EXPORT_SYMBOL(sensor_set_trip);

int sensor_cancel_trip(uint32_t sensor_id, struct sensor_threshold *threshold)
{
	struct sensor_threshold *pos = NULL, *var = NULL;
	struct sensor_info *sensor = get_sensor(sensor_id);
	int ret = 0;

	if (!sensor)
		return -ENODEV;

	mutex_lock(&sensor->lock);
	list_for_each_entry_safe(pos, var, &sensor->threshold_list, list) {
		if (pos == threshold) {
			pos->active = 0;
			list_del_rcu(&pos->list);
			break;
		}
	}

	ret = __update_sensor_thresholds(sensor);
	mutex_unlock(&sensor->lock);
	synchronize_rcu();

	return ret;
}
EXPORT_SYMBOL(sensor_cancel_trip);

static int tz_notify_trip(enum thermal_trip_type type, int temp, void *data)
{
	struct thermal_zone_device *tz = (struct thermal_zone_device *)data;

	pr_debug("sensor %d tripped: type %d temp %d\n",
			tz->sensor.sensor_id, type, temp);

	return 0;
}

static void get_trip_threshold(struct thermal_zone_device *tz, int trip,
	struct sensor_threshold **threshold)
{
	enum thermal_trip_type type;

	tz->ops->get_trip_type(tz, trip, &type);

	if (type == THERMAL_TRIP_CONFIGURABLE_HI)
		*threshold = &tz->tz_threshold[0];
	else if (type == THERMAL_TRIP_CONFIGURABLE_LOW)
		*threshold = &tz->tz_threshold[1];
	else
		*threshold = NULL;
}

int sensor_set_trip_temp(struct thermal_zone_device *tz,
		int trip, long temp)
{
	int ret = 0;
	struct sensor_threshold *threshold = NULL;

	if (!tz->ops->get_trip_type)
		return -EPERM;

	get_trip_threshold(tz, trip, &threshold);
	if (threshold) {
		threshold->temp = temp;
		ret = sensor_set_trip(tz->sensor.sensor_id, threshold);
	} else {
		ret = tz->ops->set_trip_temp(tz, trip, temp);
	}

	return ret;
}

int sensor_set_trip_enabled(struct thermal_zone_device *tz, int trip, bool enable)
{
	struct sensor_threshold *threshold;

	get_trip_threshold(tz, trip, &threshold);
	if (threshold)
		return sensor_activate_trip(tz->sensor.sensor_id, threshold, enable);
	return tz->ops->activate_trip_type(tz, trip, enable ?
		THERMAL_TRIP_ACTIVATION_ENABLED : THERMAL_TRIP_ACTIVATION_DISABLED);
}

int sensor_init(struct thermal_zone_device *tz)
{
	struct sensor_info *sensor = &tz->sensor;

	sensor->sensor_id = tz->id;
	sensor->tz = tz;
	sensor->threshold_min = INT_MIN;
	sensor->threshold_max = INT_MAX;
	sensor->max_idx = -1;
	sensor->min_idx = -1;
	mutex_init(&sensor->lock);
	INIT_LIST_HEAD_RCU(&sensor->sensor_list);
	INIT_LIST_HEAD_RCU(&sensor->threshold_list);
	INIT_LIST_HEAD(&tz->tz_threshold[0].list);
	INIT_LIST_HEAD(&tz->tz_threshold[1].list);
	tz->tz_threshold[0].notify = tz_notify_trip;
	tz->tz_threshold[0].data = tz;
	tz->tz_threshold[0].trip = THERMAL_TRIP_CONFIGURABLE_HI;
	tz->tz_threshold[1].notify = tz_notify_trip;
	tz->tz_threshold[1].data = tz;
	tz->tz_threshold[1].trip = THERMAL_TRIP_CONFIGURABLE_LOW;
	INIT_WORK(&sensor->work, sensor_update_work);
	mutex_lock(&sensor_list_lock);
	list_add_rcu(&sensor->sensor_list, &sensor_info_list);
	mutex_unlock(&sensor_list_lock);


	return 0;
}

void sensor_exit(struct thermal_zone_device *tz)
{
	mutex_lock(&sensor_list_lock);
	list_del_rcu(&tz->sensor.sensor_list);
	mutex_unlock(&sensor_list_lock);
	synchronize_rcu();
	cancel_work_sync(&tz->sensor.work);
}
