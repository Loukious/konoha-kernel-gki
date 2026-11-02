/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_path_control.h
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

#ifndef __PATH_CONTROL_H__
#define __PATH_CONTROL_H__

enum path_control_attr_list {
	USB_IN_PATH_CONTROL = 0,
	WLS_IN_PATH_CONTROL,
	WLS_REV_PATH_CONTROL,
	OTG_IN_PATH_CONTROL,
	WLS_VDD_PATH_CONTROL,
	TOTAL_PATH_CONTROL,
	PATH_CONTROL_MAX,
};

#define PATH_CONDITION_MAX_GROUP 16
enum path_control_condition_para_ele {
	CONTROL_CONDITION = 0,
	CONTROL_PATH,
	CONTROL_PARA_MAX,
};

enum boost_type_info {
	OTG_BOOST_TYPE = 0,
	WLS_REV_BOOST_TYPE,
	BOOST_TYPE_MAX,
};

#define PATH_PROCESS_BOOST_PARA_MAX 2
#define PATH_PROCESS_MAX_GROUP 16
enum path_control_process_para_ele {
	BOOST_TYPE = 0,
	BOOST_SOURCE,
	PROCESS_METHOD = PATH_PROCESS_BOOST_PARA_MAX * BOOST_TYPE_MAX,
	PROCESS_PARA_MAX,
};

enum path_control_role_para {
	OVPGATE_ROLE = 0,
	WPCGATE_ROLE,
	EXT_BST_GATE_ROLE,
	WLS_VDD_GATE_ROLE,
	HBST_GATE_ROLE,
	MAX_ROLE,
};

enum path_control_scheme {
	GPIO_SCHEME = 0,
	PMIC_REGISTER_SCHEME,
	CP_CHIP_SCHEME,
	MAX_SCHEME,
};

enum path_control_scheme_item {
	GATE_ROLE = 0,
	CONTROL_SCHEME,
	PROCESS_WAY,
	ITEM_MAX,
};

enum ext_chip_project {
	CP_PROJECT = 0,
	PMIC_PROJECT,
	MAX_PROJECT,
};

enum control_path_para_ele {
	CONTROL_GATE_PARA_GATE = 0,
	CONTROL_GATE_PARA_ENABLE,
	CONTROL_GATE_PARA_MAX,
};

struct gate_enable_info {
	int control_gate_role;
	int control_gate_enable;
};

struct path_control_boost_para {
	int boost_type[BOOST_TYPE_MAX];
	int gate_enable_para_size;
	struct gate_enable_info *gate_enable_info;
};

struct path_control_condition_cfg {
	int condition;
	int boost_cfg_num;
	struct path_control_boost_para *boost_cfg;
};

struct path_control_scheme_cfg {
	bool present;
	bool managed;
	int role;
	int scheme;

	int gpio;
	int reg;
	int cp_chip_role;
};

struct mca_path_control {
	struct device *dev;
	/* Serializes hardware controls, initial snapshot and stopping. */
	struct mutex enable_handling_lock;
	struct workqueue_struct *workqueue;
	bool stopping;
	bool ready;
	bool instance_claimed;
	bool sysfs_registered;
	bool gate_known[MAX_ROLE];
	int condition_count;
	struct delayed_work update_condition_work;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_stat;
	int gate_sts[MAX_ROLE];
	int condition_value;

	int boost_src_default[BOOST_TYPE_MAX];
	int otg_boost_enable_sts;
	int usb_online;
	int wireless_online;
	bool wireless_rev_en;
	bool wireless_vdd_en;

	struct path_control_scheme_cfg control_scheme[MAX_ROLE];
	struct path_control_condition_cfg control_path[PATH_CONDITION_MAX_GROUP];
};

#endif /* __PATH_CONTROL_H__ */
