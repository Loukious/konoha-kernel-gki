// SPDX-License-Identifier: GPL-2.0
/*
 * mca_path_control.c
 *
 * path control driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/workqueue.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/hardware/hw_path_control.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/strategy/strategy_class.h>
#ifdef CONFIG_MCA_PATH_WLS_COORDINATION
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#endif
#include "inc/mca_path_control.h"

#define MCA_LOG_TAG "mca_path_control"
#define PATH_MASK_ALL (BIT(PATH_CONTROL_VDD + 1) - 1)
#define PATH_MASK_WIRELESS                                   \
	(BIT(PATH_CONTROL_WLS) | BIT(PATH_CONTROL_WLS_REV) | \
	 BIT(PATH_CONTROL_VDD))

static DEFINE_MUTEX(path_instance_lock);
DEFINE_STATIC_SRCU(path_instance_srcu);
static struct mca_path_control __rcu *path_instance;
static bool path_claimed;

static int path_string_int(struct device_node *node, const char *name,
			   int index, int *value)
{
	const char *text;
	int ret = of_property_read_string_index(node, name, index, &text);

	return ret ? ret : kstrtoint(text, 10, value);
}

static int path_string_count(struct device_node *node, const char *name,
			     int columns, int rows)
{
	int count = of_property_count_strings(node, name);

	if (count < 0)
		return count;
	return !count || count > columns * rows || count % columns ? -EINVAL : count;
}

static int path_parse_schemes(struct mca_path_control *info)
{
	struct device_node *node = info->dev->of_node;
	struct path_control_scheme_cfg *scheme;
	const char *method;
	u32 chip;
	int count, row, role, type, ret, gpio;

	count = path_string_count(node, "control_scheme", ITEM_MAX, MAX_ROLE);
	if (count < 0)
		return count;
	for (row = 0; row < count; row += ITEM_MAX) {
		ret = path_string_int(node, "control_scheme", row, &role);
		if (ret)
			return ret;
		ret = path_string_int(node, "control_scheme", row + 1, &type);
		if (ret)
			return ret;
		if (role < 0 || role >= MAX_ROLE || type < 0 ||
		    type >= MAX_SCHEME || info->control_scheme[role].present)
			return -EINVAL;
		ret = of_property_read_string_index(node, "control_scheme",
						    row + 2, &method);
		if (ret)
			return ret;
		scheme = &info->control_scheme[role];
		scheme->present = true;
		scheme->role = role;
		scheme->scheme = type;
		if (!strcmp(method, "null"))
			continue;
		switch (type) {
		case GPIO_SCHEME:
			/* Keep the exact named GPIO binding from the pinned table. */
			gpio = of_get_named_gpio(node, method, 0);
			if (gpio < 0)
				return gpio;
			if (!gpio_is_valid(gpio))
				return -EINVAL;
			ret = devm_gpio_request_one(info->dev, gpio,
						    GPIOF_OUT_INIT_LOW, method);
			if (ret)
				return ret;
			scheme->gpio = gpio;
			info->gate_known[role] = true;
			break;
		case CP_CHIP_SCHEME:
			if (role != OVPGATE_ROLE)
				return -EOPNOTSUPP;
			if (of_property_count_u32_elems(node, method) != 1)
				return -EINVAL;
			ret = of_property_read_u32(node, method, &chip);
			if (ret)
				return ret;
			if (chip >= CP_ROLE_MAX)
				return -ERANGE;
			scheme->cp_chip_role = chip;
			break;
		default:
			return -EOPNOTSUPP;
		}
		scheme->managed = true;
	}
	return 0;
}

static int path_parse_actions(struct mca_path_control *info, const char *name,
			      struct path_control_boost_para *boost)
{
	struct device_node *node = info->dev->of_node;
	struct gate_enable_info *action;
	bool seen[MAX_ROLE] = { false };
	int count, row, role, enable, ret;

	if (!strcmp(name, "null"))
		return 0;
	count = path_string_count(node, name, CONTROL_GATE_PARA_MAX, MAX_ROLE);
	if (count < 0)
		return count;
	boost->gate_enable_para_size = count / CONTROL_GATE_PARA_MAX;
	boost->gate_enable_info = devm_kcalloc(info->dev,
					       boost->gate_enable_para_size,
					       sizeof(*action), GFP_KERNEL);
	if (!boost->gate_enable_info)
		return -ENOMEM;
	for (row = 0; row < boost->gate_enable_para_size; row++) {
		ret = path_string_int(node, name, row * CONTROL_GATE_PARA_MAX,
				      &role);
		if (ret)
			return ret;
		ret = path_string_int(node, name,
				      row * CONTROL_GATE_PARA_MAX + 1, &enable);
		if (ret)
			return ret;
		if (role < 0 || role >= MAX_ROLE ||
		    (enable != 0 && enable != 1) || seen[role] ||
		    !info->control_scheme[role].present)
			return -EINVAL;
		seen[role] = true;
		action = &boost->gate_enable_info[row];
		action->control_gate_role = role;
		action->control_gate_enable = enable;
	}
	return 0;
}

static int path_parse_boosts(struct mca_path_control *info, const char *name,
			     struct path_control_condition_cfg *condition)
{
	struct device_node *node = info->dev->of_node;
	struct path_control_boost_para *boost;
	const char *method;
	int count, row, column, type, source, ret;

	if (!strcmp(name, "null"))
		return 0;
	count = path_string_count(node, name, PROCESS_PARA_MAX,
				  PATH_PROCESS_MAX_GROUP);
	if (count < 0)
		return count;
	condition->boost_cfg_num = count / PROCESS_PARA_MAX;
	condition->boost_cfg = devm_kcalloc(info->dev, condition->boost_cfg_num,
					    sizeof(*boost), GFP_KERNEL);
	if (!condition->boost_cfg)
		return -ENOMEM;
	for (row = 0; row < condition->boost_cfg_num; row++) {
		bool seen[BOOST_TYPE_MAX] = { false };

		boost = &condition->boost_cfg[row];
		for (column = 0; column < BOOST_TYPE_MAX; column++)
			boost->boost_type[column] = -1;
		for (column = 0; column < PROCESS_METHOD;
		     column += PATH_PROCESS_BOOST_PARA_MAX) {
			ret = path_string_int(node, name,
					      row * PROCESS_PARA_MAX + column,
					      &type);
			if (ret)
				return ret;
			ret = path_string_int(
				node, name, row * PROCESS_PARA_MAX + column + 1,
				&source);
			if (ret)
				return ret;
			if (type < 0 || type >= BOOST_TYPE_MAX || seen[type] ||
			    source < -1 || source >= BOOST_SRC_MAX)
				return -EINVAL;
			seen[type] = true;
			boost->boost_type[type] = source;
		}
		ret = of_property_read_string_index(
			node, name, row * PROCESS_PARA_MAX + PROCESS_METHOD,
			&method);
		if (ret)
			return ret;
		ret = path_parse_actions(info, method, boost);
		if (ret)
			return ret;
	}
	return 0;
}

static int path_parse_dt(struct mca_path_control *info)
{
	struct device_node *node = info->dev->of_node;
	const char *name;
	int count, row, condition, ret, i;
	bool default_present = false;

	if (!node)
		return -EINVAL;
	ret = path_parse_schemes(info);
	if (ret)
		return ret;
	count = path_string_count(node, "path_condition", CONTROL_PARA_MAX,
				  PATH_CONDITION_MAX_GROUP);
	if (count < 0)
		return count;
	info->condition_count = count / CONTROL_PARA_MAX;
	for (row = 0; row < info->condition_count; row++) {
		ret = path_string_int(node, "path_condition",
				      row * CONTROL_PARA_MAX, &condition);
		if (ret)
			return ret;
		if (condition < 0 || condition > PATH_MASK_ALL)
			return -ERANGE;
		for (i = 0; i < row; i++)
			if (info->control_path[i].condition == condition)
				return -EINVAL;
		info->control_path[row].condition = condition;
		default_present |= !condition;
		ret = of_property_read_string_index(node, "path_condition",
						    row * CONTROL_PARA_MAX + 1,
						    &name);
		if (ret)
			return ret;
		ret = path_parse_boosts(info, name, &info->control_path[row]);
		if (ret)
			return ret;
	}
	return default_present ? 0 : -EINVAL;
}

static int path_control_gate(struct mca_path_control *info, int role,
			     bool enable)
{
	struct path_control_scheme_cfg *scheme = &info->control_scheme[role];
	int ret;

	if (!scheme->managed)
		return 0;
	if (info->gate_known[role] && info->gate_sts[role] == enable)
		return 0;
	switch (scheme->scheme) {
	case GPIO_SCHEME:
		ret = gpio_direction_output(scheme->gpio, enable);
		break;
	case CP_CHIP_SCHEME:
		ret = platform_class_cp_enable_ovpgate(scheme->cp_chip_role,
						       enable);
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (ret)
		return ret;
	info->gate_sts[role] = enable;
	info->gate_known[role] = true;
	return 0;
}

static int path_apply_condition(struct mca_path_control *info, int value)
{
	struct path_control_condition_cfg *condition = NULL;
	struct path_control_boost_para *boost;
	int row, column, action, ret;

	if (value < 0 || value > PATH_MASK_ALL)
		return -ERANGE;
#ifndef CONFIG_MCA_PATH_WLS_COORDINATION
	if (value & PATH_MASK_WIRELESS)
		return -EOPNOTSUPP;
#endif
	for (row = 0; row < info->condition_count; row++)
		if (info->control_path[row].condition == value) {
			condition = &info->control_path[row];
			break;
		}
	if (!condition)
		return -ENOENT;
	if (!condition->boost_cfg_num) {
		info->condition_value = value;
		return 0;
	}
	for (row = 0; row < condition->boost_cfg_num; row++) {
		boost = &condition->boost_cfg[row];
		for (column = 0; column < BOOST_TYPE_MAX; column++)
			if (boost->boost_type[column] != -1 &&
			    boost->boost_type[column] !=
				    info->boost_src_default[column])
				break;
		if (column != BOOST_TYPE_MAX)
			continue;
		for (action = 0; action < boost->gate_enable_para_size;
		     action++) {
			ret = path_control_gate(info,
						boost->gate_enable_info[action]
							.control_gate_role,
						boost->gate_enable_info[action]
							.control_gate_enable);
			if (ret)
				return ret;
		}
		info->condition_value = value;
		return 0;
	}
	return -ENOENT;
}

static int path_read_initial_status(struct mca_path_control *info)
{
	int sources[BOOST_TYPE_MAX] = { 0 };
	int usb, otg, value, ret;
#ifdef CONFIG_MCA_PATH_WLS_COORDINATION
	int wireless;
	bool reverse;
#endif

	if (info->ready)
		return 0;
	ret = platform_class_buckchg_ops_get_otg_boost_src(
		MAIN_BUCK_CHARGER, &sources[OTG_BOOST_TYPE]);
	if (ret)
		return ret;
	ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb);
	if (ret)
		return ret;
	ret = platform_class_buckchg_ops_get_otg_boost_enable_status(
		MAIN_BUCK_CHARGER, &otg);
	if (ret)
		return ret;
	if ((usb != 0 && usb != 1) || otg < OTG_DISABLE ||
	    otg > OTG_ENABLE_SEQUENCE || sources[OTG_BOOST_TYPE] < 0 ||
	    sources[OTG_BOOST_TYPE] >= BOOST_SRC_MAX)
		return -ERANGE;
	value = usb ? BIT(PATH_CONTROL_USB) : 0;
	if (otg != OTG_DISABLE)
		value |= BIT(PATH_CONTROL_OTG);
#ifdef CONFIG_MCA_PATH_WLS_COORDINATION
	ret = mca_wireless_rev_get_rev_boost_default(
		&sources[WLS_REV_BOOST_TYPE]);
	if (ret)
		return ret;
	ret = platform_class_wireless_is_present(WIRELESS_ROLE_MASTER,
						 &wireless);
	if (ret)
		return ret;
	ret = mca_wireless_rev_get_reverse_chg(&reverse);
	if (ret)
		return ret;
	if ((wireless != 0 && wireless != 1) ||
	    sources[WLS_REV_BOOST_TYPE] < 0 || sources[WLS_REV_BOOST_TYPE] >= BOOST_SRC_MAX)
		return -ERANGE;
	if (wireless)
		value |= BIT(PATH_CONTROL_WLS);
	if (reverse)
		value |= BIT(PATH_CONTROL_WLS_REV);
#endif
	memcpy(info->boost_src_default, sources, sizeof(sources));
	ret = path_apply_condition(info, value);
	if (!ret)
		info->ready = true;
	return ret;
}

static int path_set_source(struct mca_path_control *info, CONTROL_SRC src,
			   bool enable)
{
	int ret, value;

	if (src < PATH_CONTROL_USB || src > PATH_CONTROL_VDD)
		return -EINVAL;
#ifndef CONFIG_MCA_PATH_WLS_COORDINATION
	if (BIT(src) & PATH_MASK_WIRELESS)
		return -EOPNOTSUPP;
#endif
	ret = path_read_initial_status(info);
	if (ret)
		return ret;
	value = enable ? info->condition_value | BIT(src) :
			 info->condition_value & ~BIT(src);
	return path_apply_condition(info, value);
}

int mca_path_control_enable_gate(CONTROL_SRC src, bool enable)
{
	struct mca_path_control *info;
	int index, ret;

	index = srcu_read_lock(&path_instance_srcu);
	info = srcu_dereference(path_instance, &path_instance_srcu);
	if (!info) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&info->enable_handling_lock);
	ret = info->stopping ? -ENODEV : path_set_source(info, src, enable);
	mutex_unlock(&info->enable_handling_lock);
out:
	srcu_read_unlock(&path_instance_srcu, index);
	return ret;
}
EXPORT_SYMBOL(mca_path_control_enable_gate);

static void mca_path_control_update_status_work(struct work_struct *work)
{
	struct mca_path_control *info = container_of(
		work, struct mca_path_control, update_condition_work.work);
	int ret;

	mutex_lock(&info->enable_handling_lock);
	if (info->stopping)
		goto out;
	ret = path_read_initial_status(info);
	if (ret) {
		mca_log_err("initial path status failed: %d\n", ret);
		queue_delayed_work(info->workqueue,
				   &info->update_condition_work,
				   msecs_to_jiffies(1000));
	}
out:
	mutex_unlock(&info->enable_handling_lock);
}

#ifdef CONFIG_SYSFS
static ssize_t path_control_sysfs_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);
static ssize_t path_control_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);
static struct mca_sysfs_attr_info path_control_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(path_control_sysfs, 0664, USB_IN_PATH_CONTROL,
			  usb_in),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, WLS_IN_PATH_CONTROL,
			  wls_in),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, WLS_REV_PATH_CONTROL,
			  wls_rev),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, OTG_IN_PATH_CONTROL,
			  otg_in),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, WLS_VDD_PATH_CONTROL,
			  wls_vdd),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, TOTAL_PATH_CONTROL,
			  control_value),
};
#define PATH_CONTROL_SYSFS_ATTRS_SIZE ARRAY_SIZE(path_control_sysfs_field_tbl)
static struct attribute
	*path_control_sysfs_attrs[PATH_CONTROL_SYSFS_ATTRS_SIZE + 1];
static const struct attribute_group path_control_sysfs_attr_group = {
	.attrs = path_control_sysfs_attrs,
};

static ssize_t path_control_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct mca_path_control *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *entry;
	int value, ret;

	if (!info)
		return -ENODEV;
	entry = mca_sysfs_lookup_attr(attr->attr.name,
				      path_control_sysfs_field_tbl,
				      PATH_CONTROL_SYSFS_ATTRS_SIZE);
	if (!entry || entry->sysfs_attr_name > TOTAL_PATH_CONTROL)
		return -EINVAL;
	mutex_lock(&info->enable_handling_lock);
	if (info->stopping)
		ret = -ENODEV;
	else if (!info->ready)
		ret = -EAGAIN;
	else {
		value = entry->sysfs_attr_name == TOTAL_PATH_CONTROL ?
				info->condition_value :
				!!(info->condition_value &
				   BIT(entry->sysfs_attr_name));
		ret = sysfs_emit(buf, "%d\n", value);
	}
	mutex_unlock(&info->enable_handling_lock);
	return ret;
}

static ssize_t path_control_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mca_path_control *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *entry;
	int value, ret;

	if (!info)
		return -ENODEV;
	if (!count || memchr(buf, '\0', count))
		return -EINVAL;
	ret = kstrtoint(buf, 10, &value);
	if (ret)
		return ret;
	entry = mca_sysfs_lookup_attr(attr->attr.name,
				      path_control_sysfs_field_tbl,
				      PATH_CONTROL_SYSFS_ATTRS_SIZE);
	if (!entry || entry->sysfs_attr_name > TOTAL_PATH_CONTROL)
		return -EINVAL;
	mutex_lock(&info->enable_handling_lock);
	if (info->stopping) {
		ret = -ENODEV;
	} else if (entry->sysfs_attr_name == TOTAL_PATH_CONTROL) {
		if (value < 0 || value > PATH_MASK_ALL) {
			ret = -ERANGE;
			goto out;
		}
#ifndef CONFIG_MCA_PATH_WLS_COORDINATION
		if (value & PATH_MASK_WIRELESS) {
			ret = -EOPNOTSUPP;
			goto out;
		}
#endif
		ret = path_read_initial_status(info);
		if (!ret)
			ret = path_apply_condition(info, value);
	} else if (value != 0 && value != 1) {
		ret = -ERANGE;
	} else {
		ret = path_set_source(info, entry->sysfs_attr_name, value);
	}
out:
	mutex_unlock(&info->enable_handling_lock);
	return ret ? ret : count;
}

static int mca_path_control_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(path_control_sysfs_attrs,
			     path_control_sysfs_field_tbl,
			     PATH_CONTROL_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "path_control", dev,
					   &path_control_sysfs_attr_group);
}

static void mca_path_control_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_5, "path_control", dev,
				    &path_control_sysfs_attr_group);
}
#else
static int mca_path_control_sysfs_create_group(struct device *dev)
{
	return 0;
}

static void mca_path_control_sysfs_remove_group(struct device *dev)
{
}
#endif

static void path_stop(struct mca_path_control *info)
{
	if (!info)
		return;
	mutex_lock(&info->enable_handling_lock);
	if (info->stopping) {
		mutex_unlock(&info->enable_handling_lock);
		return;
	}
	info->stopping = true;
	mutex_unlock(&info->enable_handling_lock);
	mutex_lock(&path_instance_lock);
	if (rcu_access_pointer(path_instance) == info)
		rcu_assign_pointer(path_instance, NULL);
	mutex_unlock(&path_instance_lock);
	synchronize_srcu(&path_instance_srcu);
	if (info->sysfs_registered)
		mca_path_control_sysfs_remove_group(info->dev);
	if (info->workqueue) {
		cancel_delayed_work_sync(&info->update_condition_work);
		destroy_workqueue(info->workqueue);
		info->workqueue = NULL;
	}
	mutex_lock(&path_instance_lock);
	if (info->instance_claimed) {
		path_claimed = false;
		info->instance_claimed = false;
	}
	mutex_unlock(&path_instance_lock);
}

static int mca_path_control_probe(struct platform_device *pdev)
{
	struct mca_path_control *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	mutex_init(&info->enable_handling_lock);
	INIT_DELAYED_WORK(&info->update_condition_work,
			  mca_path_control_update_status_work);
	mutex_lock(&path_instance_lock);
	if (path_claimed) {
		mutex_unlock(&path_instance_lock);
		return -EBUSY;
	}
	path_claimed = true;
	info->instance_claimed = true;
	mutex_unlock(&path_instance_lock);
	platform_set_drvdata(pdev, info);
	ret = path_parse_dt(info);
	if (ret)
		goto fail;
	info->workqueue =
		alloc_ordered_workqueue("mca_path_control", WQ_MEM_RECLAIM);
	if (!info->workqueue) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = mca_path_control_sysfs_create_group(info->dev);
	if (ret)
		goto fail;
	info->sysfs_registered = true;
	mutex_lock(&path_instance_lock);
	rcu_assign_pointer(path_instance, info);
	mutex_unlock(&path_instance_lock);
	mutex_lock(&info->enable_handling_lock);
	queue_delayed_work(info->workqueue, &info->update_condition_work,
			   msecs_to_jiffies(10000));
	mutex_unlock(&info->enable_handling_lock);
	return 0;
fail:
	path_stop(info);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static void mca_path_control_remove(struct platform_device *pdev)
{
	path_stop(platform_get_drvdata(pdev));
	platform_set_drvdata(pdev, NULL);
}

static void mca_path_control_shutdown(struct platform_device *pdev)
{
	path_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,path_control" },
	{},
};
MODULE_DEVICE_TABLE(of, match_table);
static struct platform_driver mca_path_control_driver = {
	.driver = { .name = "mca_path_control", .of_match_table = match_table },
	.probe = mca_path_control_probe,
	.remove = mca_path_control_remove,
	.shutdown = mca_path_control_shutdown,
};
module_platform_driver(mca_path_control_driver);
MODULE_DESCRIPTION("MCA Path Control");
MODULE_AUTHOR("wuliyang@xiaomi.com");
MODULE_LICENSE("GPL v2");
