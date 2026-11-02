// SPDX-License-Identifier: GPL-2.0
/*
 * mca_bmd.c
 *
 * battery missing detection driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
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
#include <linux/iio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include "inc/mca_bmd.h"

#define MCA_LOG_TAG "mca_bmd"
#define BTB_OPEN_MAX_VOL 1900
#define BTB_OPEN_MIN_VOL 1700
#define MONITOR_HEARTBEAT_TIMER_MS 500
#define DELAY_REPORT_BMD_STS_MS 2500
#define MCA_BMD_DUAL_FG 1

enum bmd_scheme { ADC_SCHEME, GPIO_SCHEME, INT_SCHEME, IIC_SCHEME, MAX_SCHEME };

struct bmd_scheme_data {
	int scheme;
	struct iio_channel *channel;
	int gpio;
};

struct mca_bmd_dev {
	struct device *dev;
	struct mutex policy_lock;
	spinlock_t control_lock;
	struct workqueue_struct *workqueue;
	struct bmd_scheme_data bmd_scheme[MAX_BTB];
	struct delayed_work monitor_bmd_work;
	struct delayed_work delay_report_bmd_sts_work;
	bool btb_online[MAX_BTB];
	bool batt_missing;
	bool stopping;
	bool strategy_registered;
	bool sysfs_registered;
	int fake_batt;
	int fg_type;
};

static int mca_bmd_get_btb_status(struct mca_bmd_dev *info, int index,
				  bool *online)
{
	int value, ret;

	if (!info || !online || index < 0 || index >= MAX_BTB)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	switch (info->bmd_scheme[index].scheme) {
	case ADC_SCHEME:
		ret = iio_read_channel_processed(
			info->bmd_scheme[index].channel, &value);
		if (ret < 0)
			return ret;
		if (value < 0)
			return -ERANGE;
		*online =
			!(value > BTB_OPEN_MIN_VOL && value < BTB_OPEN_MAX_VOL);
		return 0;
	case GPIO_SCHEME:
		/* Preserve the stock btb_gpio binding and its physical low = present. */
		value = gpio_get_value_cansleep(info->bmd_scheme[index].gpio);
		break;
	case IIC_SCHEME:
		value = info->fg_type == MCA_BMD_DUAL_FG ?
				strategy_class_fg_dual_is_chip_ok(index) :
				strategy_class_fg_is_chip_ok();
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (value < 0)
		return value;
	if (value > 1)
		return -ERANGE;
	*online = info->bmd_scheme[index].scheme == GPIO_SCHEME ? !value :
								  !!value;
	return 0;
}

static int mca_bmd_get_status(int type, void *value, void *data)
{
	struct mca_bmd_dev *info = data;

	if (!info || !value || type != STRATEGY_STATUS_TYPE_BMD_BATT_MISSING)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	*(u32 *)value = READ_ONCE(info->batt_missing);
	return 0;
}

static void mca_bmd_update_status(struct mca_bmd_dev *info, bool force_report)
{
	bool online[MAX_BTB] = { false, false };
	bool missing;
	int fake, report, master_ret, slave_ret;

	master_ret =
		mca_bmd_get_btb_status(info, MASTER_BTB, &online[MASTER_BTB]);
	slave_ret = mca_bmd_get_btb_status(info, SLAVE_BTB, &online[SLAVE_BTB]);
	missing = !online[MASTER_BTB] || !online[SLAVE_BTB];
	if (master_ret || slave_ret)
		mca_log_err("BMD read failed: master %d slave %d\n", master_ret,
			    slave_ret);
	info->btb_online[MASTER_BTB] = online[MASTER_BTB];
	info->btb_online[SLAVE_BTB] = online[SLAVE_BTB];
	if (force_report || READ_ONCE(info->batt_missing) != missing) {
		WRITE_ONCE(info->batt_missing, missing);
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
				       MCA_EVENT_BATT_BTB_CHANGE, &missing);
		if (!missing) {
			mca_charge_mievent_set_state(
				MIEVENT_STATE_END,
				info->fg_type == MCA_BMD_DUAL_FG ?
					CHARGE_DFX_DUAL_BATTERY_MISSING :
					CHARGE_DFX_BATTERY_MISSING);
		} else if (info->fg_type == MCA_BMD_DUAL_FG) {
			report = online[MASTER_BTB];
			mca_charge_mievent_report(
				CHARGE_DFX_DUAL_BATTERY_MISSING, &report, 1);
		} else {
			mca_charge_mievent_report(CHARGE_DFX_BATTERY_MISSING,
						  NULL, 0);
		}
	}
	fake = info->fg_type == MCA_BMD_DUAL_FG ?
		       missing :
		       !online[MASTER_BTB] && !online[SLAVE_BTB];
	if (master_ret || slave_ret)
		fake = 1;
	if (force_report || info->fake_batt != fake) {
		info->fake_batt = fake;
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_FAKE_POWER, &fake);
	}
}

static void mca_bmd_monitor_workfunc(struct work_struct *work)
{
	struct mca_bmd_dev *info =
		container_of(work, struct mca_bmd_dev, monitor_bmd_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping))
		return;
	mca_bmd_update_status(info, false);
	mod_delayed_work(info->workqueue, &info->monitor_bmd_work,
			 msecs_to_jiffies(MONITOR_HEARTBEAT_TIMER_MS));
}

static void mca_bmd_delay_report_bmd_sts_work(struct work_struct *work)
{
	struct mca_bmd_dev *info = container_of(work, struct mca_bmd_dev,
						delay_report_bmd_sts_work.work);

	guard(mutex)(&info->policy_lock);
	if (!READ_ONCE(info->stopping))
		mca_bmd_update_status(info, true);
}

static int mca_bmd_process_event(int event, int value, void *data)
{
	struct mca_bmd_dev *info = data;
	unsigned long flags;
	int ret = 0;

	if (!info)
		return -EINVAL;
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		break;
	default:
		return -EOPNOTSUPP;
	}
	spin_lock_irqsave(&info->control_lock, flags);
	if (info->stopping)
		ret = -ENODEV;
	else
		mod_delayed_work(info->workqueue, &info->monitor_bmd_work, 0);
	spin_unlock_irqrestore(&info->control_lock, flags);
	return ret;
}

static int mca_bmd_parse_dt(struct mca_bmd_dev *info)
{
	struct device_node *node = info->dev->of_node;
	int values[MAX_BTB], i, ret;
	int owned_gpio = -1;

	if (!node ||
	    of_property_count_u32_elems(node, "btb_bmd_scheme") != MAX_BTB)
		return -EINVAL;
	ret = mca_parse_dts_u32_array(node, "btb_bmd_scheme", values, MAX_BTB);
	if (ret)
		return ret;
	/* Missing fg_type selects the native single-fuel-gauge default. */
	info->fg_type = 0;
	if (of_property_present(node, "fg_type")) {
		if (of_property_count_u32_elems(node, "fg_type") != 1)
			return -EINVAL;
		ret = mca_parse_dts_u32(node, "fg_type", &info->fg_type, 0);
		if (ret)
			return ret;
	}
	if (info->fg_type != 0 && info->fg_type != MCA_BMD_DUAL_FG)
		return -EINVAL;
	for (i = 0; i < MAX_BTB; i++) {
		info->bmd_scheme[i].scheme = values[i];
		switch (values[i]) {
		case ADC_SCHEME:
			info->bmd_scheme[i].channel =
				devm_iio_channel_get(info->dev, "btb_adc");
			if (IS_ERR(info->bmd_scheme[i].channel))
				return PTR_ERR(info->bmd_scheme[i].channel);
			break;
		case GPIO_SCHEME:
			ret = of_get_named_gpio(node, "btb_gpio", 0);
			if (ret < 0)
				return ret;
			info->bmd_scheme[i].gpio = ret;
			if (owned_gpio == ret)
				break;
			/* The vendor underscore property is not a descriptor lookup alias. */
			ret = devm_gpio_request_one(info->dev, ret, GPIOF_IN,
						    "btb_gpio");
			if (ret)
				return ret;
			owned_gpio = info->bmd_scheme[i].gpio;
			break;
		case IIC_SCHEME:
			break;
		default:
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

enum btb_attr_list { BTB_PROP_MASTER, BTB_PROP_SLAVE, BTB_PROP_MISSING };
static ssize_t btb_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf);
struct mca_sysfs_attr_info btb_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(btb_sysfs, 0440, BTB_PROP_MASTER, btb_master_status),
	mca_sysfs_attr_ro(btb_sysfs, 0440, BTB_PROP_SLAVE, btb_slave_status),
	mca_sysfs_attr_ro(btb_sysfs, 0440, BTB_PROP_MISSING,
			  btb_missing_status),
};
#define BTB_SYSFS_ATTRS_SIZE ARRAY_SIZE(btb_sysfs_field_tbl)
static struct attribute *btb_sysfs_attrs[BTB_SYSFS_ATTRS_SIZE + 1];
static const struct attribute_group btb_sysfs_attr_group = {
	.attrs = btb_sysfs_attrs
};

static ssize_t btb_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct mca_bmd_dev *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *field;
	bool online;
	int ret;

	if (!info)
		return -ENODEV;
	field = mca_sysfs_lookup_attr(attr->attr.name, btb_sysfs_field_tbl,
				      BTB_SYSFS_ATTRS_SIZE);
	if (!field)
		return -EINVAL;
	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	if (field->sysfs_attr_name == BTB_PROP_MISSING)
		return sysfs_emit(buf, "%d\n", READ_ONCE(info->batt_missing));
	ret = mca_bmd_get_btb_status(info,
				     field->sysfs_attr_name == BTB_PROP_MASTER ?
					     MASTER_BTB :
					     SLAVE_BTB,
				     &online);
	return ret ? ret : sysfs_emit(buf, "%d\n", online);
}

static int btb_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(btb_sysfs_attrs, btb_sysfs_field_tbl,
			     BTB_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "bmd", dev,
					   &btb_sysfs_attr_group);
}

static void mca_bmd_stop(struct mca_bmd_dev *info)
{
	unsigned long flags;

	if (!info)
		return;
	mutex_lock(&info->policy_lock);
	spin_lock_irqsave(&info->control_lock, flags);
	if (info->stopping) {
		spin_unlock_irqrestore(&info->control_lock, flags);
		mutex_unlock(&info->policy_lock);
		return;
	}
	WRITE_ONCE(info->stopping, true);
	spin_unlock_irqrestore(&info->control_lock, flags);
	mutex_unlock(&info->policy_lock);
	if (info->strategy_registered)
		mca_strategy_ops_unregister(STRATEGY_FUNC_TYPE_BMD, info);
	if (info->sysfs_registered)
		mca_sysfs_remove_link_group(SYSFS_DEV_5, "bmd", info->dev,
					    &btb_sysfs_attr_group);
	if (info->workqueue) {
		cancel_delayed_work_sync(&info->monitor_bmd_work);
		cancel_delayed_work_sync(&info->delay_report_bmd_sts_work);
		destroy_workqueue(info->workqueue);
		info->workqueue = NULL;
	}
}

static int mca_bmd_probe(struct platform_device *pdev)
{
	struct mca_bmd_dev *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	mutex_init(&info->policy_lock);
	spin_lock_init(&info->control_lock);
	INIT_DELAYED_WORK(&info->monitor_bmd_work, mca_bmd_monitor_workfunc);
	INIT_DELAYED_WORK(&info->delay_report_bmd_sts_work,
			  mca_bmd_delay_report_bmd_sts_work);
	info->batt_missing = true;
	info->fake_batt = 1;
	ret = mca_bmd_parse_dt(info);
	if (ret)
		goto fail;
	info->workqueue = alloc_ordered_workqueue("mca_bmd", WQ_MEM_RECLAIM);
	if (!info->workqueue) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BMD,
					mca_bmd_process_event,
					mca_bmd_get_status, NULL, info);
	if (ret)
		goto fail;
	info->strategy_registered = true;
	ret = btb_sysfs_create_group(info->dev);
	if (ret)
		goto fail;
	info->sysfs_registered = true;
	mod_delayed_work(info->workqueue, &info->monitor_bmd_work, 0);
	mod_delayed_work(info->workqueue, &info->delay_report_bmd_sts_work,
			 msecs_to_jiffies(DELAY_REPORT_BMD_STS_MS));
	return 0;
fail:
	mca_bmd_stop(info);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static void mca_bmd_remove(struct platform_device *pdev)
{
	mca_bmd_stop(platform_get_drvdata(pdev));
	platform_set_drvdata(pdev, NULL);
}

static void mca_bmd_shutdown(struct platform_device *pdev)
{
	mca_bmd_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id match_table[] = { { .compatible = "mca,bmd" },
						   {} };
MODULE_DEVICE_TABLE(of, match_table);
static struct platform_driver mca_bmd_driver = {
	.driver = { .name = "mca_bmd", .of_match_table = match_table },
	.probe = mca_bmd_probe,
	.remove = mca_bmd_remove,
	.shutdown = mca_bmd_shutdown,
};
module_platform_driver(mca_bmd_driver);
MODULE_DESCRIPTION("battery missing detection");
MODULE_LICENSE("GPL v2");
