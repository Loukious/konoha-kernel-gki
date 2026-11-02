// SPDX-License-Identifier: GPL-2.0
/*
 *mca_strategy_buckchg.c
 *
 * mca buck charger strategy driver
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
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_charge_interface.h>
#include <mca/strategy/strategy_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include "inc/mca_strategy_buckchg.h"
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/strategy/strategy_soc_limit_stepper.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/common/mca_hwid.h>
//#include "hwid.h"
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_workqueue.h>
#include <mca/common/mca_smem.h>
#include <mca/common/mca_event.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_strategy_buckchg"
#endif

static void strategy_buckchg_monitor_workfunc(struct work_struct *work);
static void strategy_buckchg_sw_cv_workfunc(struct work_struct *work);
static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work);
static void strategy_wls_revchg_monitor_workfunc(struct work_struct *work);
static void strategy_csd_pulse_process_workfunc(struct work_struct *work);
static void strategy_source_status_monitor_workfunc(struct work_struct *work);
static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *work);
static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *work);
static void
strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *work);

#define CHECK_VBUS_9V_HIGH_TH 7600
#define CHECK_VBUS_5V_LOW_TH 6000
#define DEFAULT_PD_CURRENT_MA 500
#define SW_CV_FCC_STEP_DEFAULT 50
#define SW_CV_FV_STEP_DEFAULT 5
#define SW_CV_FV_COMP_TEMP_LOW 180
#define SW_CV_FV_COMP_TEMP_MID 300
#define SW_CV_FV_COMP_TEMP_HIGH 400
#define SW_CV_FV_COMP_TEMP_MAX 479
#define CP_BUSOVP_FLOAT_MV 7000
#define CP_BUSOVP_DEFAULT_MV 13000
#define CP_ADC_SETTLE_MS 10
#define CP_KEEP_BUCK_VBAT_THR 3399
#define PPS_FFC_BOOST_TEMP_THR 34
#define PPS_FFC_BOOST_TEMP_OFFSET_THR 32
#define PPS_FFC_BOOST_VBAT_THR 4401
#define PPS_FFC_IBUS_BOOST 500
#define SOC_LIMIT_ICL_DEFAULT 500
#define SOC_LIMIT_ICL_BATT_TYPE 1000
#define CP_CHIP_VENDOR_SC8541 0
#define CP_CHIP_VENDOR_BQ25960 4
#define CP_REVCHG_EXIT_TIMEOUT 40
#define BASE_FLIP_SW_CV_FV_STEP_FAST 10
#define BASE_FLIP_SW_CV_FV_STEP_SLOW 5
#define BASE_FLIP_SW_CV_VBAT_DELTA_FAST 5
#define BASE_FLIP_SW_CV_VBAT_DELTA_SLOW 3
#define BASE_FLIP_SW_CV_FCC_STEP 50
#define BASE_FLIP_SW_CV_FCC_BUFFER 100
#define BASE_FLIP_SW_CV_ITERM_DELTA 299
#define BASE_FLIP_SW_CV_OV_MAX_STEP 5

static void strategy_buckchg_set_charge_volt(struct strategy_buckchg_dev *info,
					     int target_volt);
static void strategy_buck_update_req_volt(struct strategy_buckchg_dev *info);
static void
strategy_buckchg_exit_wireless_revchg(struct strategy_buckchg_dev *info);
static void strategy_wls_revchg_monitor_workfunc(struct work_struct *work);
static int
strategy_buckchg_check_charger_change(struct strategy_buckchg_dev *info);
static void strategy_buckchg_sw_cv_start(struct strategy_buckchg_dev *info);
static void strategy_buckchg_sw_cv_stop(struct strategy_buckchg_dev *info);
static void
strategy_buckchg_process_soc_limit_change(int value,
					  struct strategy_buckchg_dev *info);

static int
strategy_class_buckchg_ops_set_input_volt(struct strategy_buckchg_dev *info,
					  int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_input_volt_lmt(MAIN_BUCK_CHARGER,
							    mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_input_volt_lmt(
			AUX_BUCK_CHARGER, mv);

	return ret;
}

static int strategy_class_buckchg_ops_set_chg(struct strategy_buckchg_dev *info,
					      bool en)
{
	int ret;

	ret = platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, en);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_chg(AUX_BUCK_CHARGER, en);

	return ret;
}

static int
strategy_class_buckchg_ops_set_opt_fws(struct strategy_buckchg_dev *info,
				       int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_opt_fws(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_opt_fws(AUX_BUCK_CHARGER,
							      mv);

	return ret;
}

static int
strategy_class_buckchg_ops_adc_enable(struct strategy_buckchg_dev *info,
				      bool en)
{
	int ret;

	ret = platform_class_buckchg_ops_adc_enable(MAIN_BUCK_CHARGER, en);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_adc_enable(AUX_BUCK_CHARGER,
							     en);

	return ret;
}

static int strategy_class_buckchg_ops_set_usb_aicl_cont_thd(
	struct strategy_buckchg_dev *info, int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_usb_aicl_cont_thd(
		MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_usb_aicl_cont_thd(
			AUX_BUCK_CHARGER, mv);

	return ret;
}

static void strategy_buckchg_parse_dt(struct strategy_buckchg_dev *info)
{
	u32 idata[REV_USBIN_TYPE_MAX] = { 0 };
	int ret = 0;

	mca_parse_dts_u32(info->dev->of_node, "support_multi_buck",
			  &info->support_multi_buck,
			  STATEGY_SUPPORT_MULTI_BUCK);
	mca_parse_dts_u32(info->dev->of_node, "in_dcp", &info->in_dcp,
			  CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "batt_type", &info->batt_type, 0);
	mca_parse_dts_u32(info->dev->of_node, "in_pps", &info->in_pps,
			  CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_pd", &info->in_pd,
			  CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_hvdcp", &info->in_hvdcp,
			  CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_hvdcp3", &info->in_hvdcp3,
			  CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_hvdcp3p5", &info->in_hvdcp3p5,
			  CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_cdp", &info->in_cdp,
			  CHARGE_CDP_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_sdp", &info->in_sdp,
			  CHARGE_SDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "in_float", &info->in_float,
			  CHARGE_FLOAT_INPUT_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_dcp", &info->chg_dcp,
			  CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_pps", &info->chg_pps,
			  CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_pd", &info->chg_pd,
			  CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_hvdcp", &info->chg_hvdcp,
			  CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_hvdcp3", &info->chg_hvdcp3,
			  CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_hvdcp3p5",
			  &info->chg_hvdcp3p5, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_cdp", &info->chg_cdp,
			  CHARGE_CDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_sdp", &info->chg_sdp,
			  CHARGE_SDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_float", &info->chg_float,
			  CHARGE_FLOAT_CHARGE_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "chg_batt_auth_failed",
			  &info->chg_batt_auth_failed,
			  CHARGE_BATT_AUTH_FAIL_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "ffc_temp_low",
			  &info->ffc_temp_low, ALLOW_FFC_TEMP_LOW_THR);
	mca_parse_dts_u32(info->dev->of_node, "ffc_temp_high",
			  &info->ffc_temp_high, ALLOW_FFC_TEMP_HIGH_THR);
	mca_parse_dts_u32(info->dev->of_node, "sw_cv_fcc_step",
			  &info->sw_cv_fcc_step, SW_CV_FCC_STEP_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "sw_cv_fv_step",
			  &info->sw_cv_fv_step, SW_CV_FV_STEP_DEFAULT);
	mca_parse_dts_u32(info->dev->of_node, "sw_cv_fcc_limit_buffer",
			  &info->sw_cv_fcc_limit_buffer, 0);
	mca_parse_dts_u32(info->dev->of_node, "allow_start_ffc_batt_soc_thr",
			  &info->allow_start_ffc_batt_soc_thr,
			  ALLOW_START_FFC_BATT_SOC_THR);
	mca_parse_dts_u32(info->dev->of_node, "curr_terminate_compensation",
			  &info->curr_terminate_compensation, 0);
	mca_parse_dts_u32(info->dev->of_node, "ffc_terminated_by_cp",
			  &info->ffc_terminated_by_cp, 0);
	mca_parse_dts_u32(info->dev->of_node, "pmic_fv_compensation",
			  &info->pmic_fv_compensation, 0);
	mca_parse_dts_u32(info->dev->of_node, "pmic_fv_compensation_cold",
			  &info->pmic_fv_compensation_cold, 0);
	mca_parse_dts_u32(info->dev->of_node, "bat_temp_fv_comp_cold_th",
			  &info->bat_temp_fv_comp_cold_th, 0);
	mca_parse_dts_u32(info->dev->of_node, "pmic_middle_fv_compensation",
			  &info->pmic_middle_fv_compensation, 0);
	mca_parse_dts_u32(info->dev->of_node, "pmic_high_fv_compensation",
			  &info->pmic_high_fv_compensation, 0);
	mca_parse_dts_u32(info->dev->of_node, "pmic_fv_compensation_hot",
			  &info->pmic_fv_compensation_hot, 0);
	mca_parse_dts_u32(info->dev->of_node, "bat_temp_fv_comp_hot_th",
			  &info->bat_temp_fv_comp_hot_th, 50);
	mca_parse_dts_u32(info->dev->of_node, "pmic_wls_fv_compensation",
			  &info->pmic_wls_fv_compensation, 0);
	mca_parse_dts_u32(info->dev->of_node, "pmic_iterm_compensation",
			  &info->pmic_iterm_compensation, 30);
	info->support_diff_temp_comp = of_property_read_bool(
		info->dev->of_node, "support_diff_temp_comp");
	info->base_flip_same =
		of_property_read_bool(info->dev->of_node, "base-flip-same");
	info->support_pmic_vterm_dynamics_adjust = of_property_read_bool(
		info->dev->of_node, "support-pmic-vterm-dynamics-adjust");
	info->support_reverse_quick_charge = of_property_read_bool(
		info->dev->of_node, "support_reverse_quick_charge");
	info->need_cp_to_pmic =
		of_property_read_bool(info->dev->of_node, "need-cp-to-pmic");
	info->support_base_flip =
		of_property_read_bool(info->dev->of_node, "support-base-flip");
	info->support_revchg_screenon = of_property_read_bool(
		info->dev->of_node, "support_revchg_screenon");
	info->support_charge_more = of_property_read_bool(
		info->dev->of_node, "support-charge-more");
	info->support_low_voltage_high_current_keep_buck_chg =
		of_property_read_bool(
			info->dev->of_node,
			"support-low-voltage-high-current-keep-buck-chg");
	mca_parse_dts_u32(info->dev->of_node, "vusb_ovp_location",
			  &info->vusb_ovp_location, 0);
	mca_parse_dts_u32(info->dev->of_node, "terminated_by_cp",
			  &info->terminated_by_cp, 0);

	ret = mca_parse_dts_u32_array(info->dev->of_node, "rev_req_vadp", idata,
				      REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_req_vadp[REV_USBIN_TYPE_PPS] =
			STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT;
		info->rev_req_vadp[REV_USBIN_TYPE_OTHER] =
			STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT;
	} else
		memcpy(info->rev_req_vadp, idata, sizeof(idata));
	mca_log_debug("rev_req_vadp %d %d\n",
		      info->rev_req_vadp[REV_USBIN_TYPE_PPS],
		      info->rev_req_vadp[REV_USBIN_TYPE_OTHER]);

	ret = mca_parse_dts_u32_array(info->dev->of_node, "rev_vadp_valid_h",
				      idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_vadp_valid_h[REV_USBIN_TYPE_PPS] =
			STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H;
		info->rev_vadp_valid_h[REV_USBIN_TYPE_OTHER] =
			STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H;
	} else
		memcpy(info->rev_vadp_valid_h, idata, sizeof(idata));
	mca_log_debug("rev_vadp_valid_h %d %d\n",
		      info->rev_vadp_valid_h[REV_USBIN_TYPE_PPS],
		      info->rev_vadp_valid_h[REV_USBIN_TYPE_OTHER]);

	ret = mca_parse_dts_u32_array(info->dev->of_node, "rev_vadp_valid_l",
				      idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_vadp_valid_l[REV_USBIN_TYPE_PPS] =
			STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L;
		info->rev_vadp_valid_l[REV_USBIN_TYPE_OTHER] =
			STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L;
	} else
		memcpy(info->rev_vadp_valid_l, idata, sizeof(idata));
	mca_log_debug("rev_vadp_valid_l %d %d\n",
		      info->rev_vadp_valid_l[REV_USBIN_TYPE_PPS],
		      info->rev_vadp_valid_l[REV_USBIN_TYPE_OTHER]);
}

static int strategy_buckchg_enable(struct mca_votable *votable, void *data,
				   int effective_result,
				   const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	mca_log_err("%d\n", effective_result);
	if (effective_result)
		return strategy_class_buckchg_ops_set_chg(info, true);
	else
		return strategy_class_buckchg_ops_set_chg(info, false);
}

static int strategy_buckchg_input_suspend(struct mca_votable *votable,
					  void *data, int effective_result,
					  const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!info)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	return mca_vote(info->input_limit_voter, "input_suspend",
			effective_result != 0, 0);
}

static int strategy_buckchg_input_voltage(struct mca_votable *votable,
					  void *data, int effective_result,
					  const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	int input, current_value, ret, current_ret;

	if (!info)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->hvdcp_allow_flag, effective_result);
	WRITE_ONCE(info->proc_data.voltage, effective_result ?
						    STATEGY_CHARGE_VBUS_9V :
						    STATEGY_CHARGE_VBUS_5V);
	input = effective_result ? READ_ONCE(info->buck_9v_in) :
				   READ_ONCE(info->buck_5v_in);
	current_value = effective_result ? READ_ONCE(info->buck_9v_ich) :
					   READ_ONCE(info->buck_5v_ich);
	ret = mca_vote(info->input_limit_voter, "volt_limit", input != 0,
		       input);
	current_ret = mca_vote(info->charge_limit_voter, "volt_thermal_limit",
			       current_value != 0, current_value);
	return ret ? ret : current_ret;
}

static int
strategy_buckchg_process_multi_input_limit(struct strategy_buckchg_dev *info,
					   int result)
{
	return -EOPNOTSUPP;
}

static int strategy_buckchg_input_limit(struct mca_votable *votable, void *data,
					int effective_result,
					const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	struct timespec64 ts;
	int is_zero_speed = 0;
	int result = effective_result;

	if (!data)
		return -EINVAL;
	if (effective_result < 0 || effective_result > 30000)
		return -ERANGE;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	ktime_get_boottime_ts64(&ts);
	if ((u64)ts.tv_sec < 60) {
		get_smem_battery_info(&is_zero_speed);
		if (is_zero_speed && effective_result) {
			result = effective_result < 1500 ? 1500 :
							   effective_result;
			mca_log_err(
				"is_zero_speed, result = %d, effective_result = %d\n",
				result, effective_result);
		}
	}

	mca_log_err("set input current %d\n", result);

	if (info->support_multi_buck)
		return strategy_buckchg_process_multi_input_limit(info, result);

	return platform_class_buckchg_ops_set_input_curr_lmt(MAIN_BUCK_CHARGER,
							     result);
}

static int
strategy_buckchg_process_multi_charge_limit(struct strategy_buckchg_dev *info,
					    int result)
{
	return -EOPNOTSUPP;
}

static int strategy_buckchg_charge_limit(struct mca_votable *votable,
					 void *data, int effective_result,
					 const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -EINVAL;
	if (effective_result < 0 || effective_result > 30000)
		return -ERANGE;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	mca_log_err("set chg current %d\n", effective_result);

	if (info->support_multi_buck)
		return strategy_buckchg_process_multi_charge_limit(
			info, effective_result);

	return platform_class_buckchg_ops_set_ichg(MAIN_BUCK_CHARGER,
						   effective_result);
}

static int
strategy_buckchg_process_multi_vterm(struct strategy_buckchg_dev *info,
				     int result)
{
	return -EOPNOTSUPP;
}

#define BASE_FLIP_VTERM_TEMP_HOT_LOW 48
#define BASE_FLIP_VTERM_TEMP_HOT_HIGH 55
#define BASE_FLIP_VTERM_TEMP_FFC_TH 35
#define BASE_FLIP_VTERM_COMP_HOT 30
#define BASE_FLIP_VTERM_COMP_FFC 40
#define BASE_FLIP_VTERM_COMP_FFC_HOT 45
#define BASE_FLIP_VTERM_COMP_IDLE 10
#define BASE_FLIP_VTERM_COMP_BASE 20
#define VTERM_DYN_TEMP_WARM 180
#define VTERM_DYN_TEMP_MIDDLE 300
#define VTERM_DYN_TEMP_HIGH 400

static int strategy_buckchg_set_vterm(struct mca_votable *votable, void *data,
				      int effective_result,
				      const char *effective_client)
{
	struct strategy_buckchg_dev *info = NULL;
	int vterm = 0;
	int batt_temp = 0;
	int compensation;
	int ret;

	if (!data)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	info = data;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	if (effective_result < 2000 || effective_result > 6000)
		return -ERANGE;

	if (info->support_base_flip) {
		if (!strategy_class_fg_get_fastcharge())
			info->pmic_fv_compensation = BASE_FLIP_VTERM_COMP_IDLE;
		else if (!info->quit_qc)
			info->pmic_fv_compensation = BASE_FLIP_VTERM_COMP_BASE;
		else
			info->pmic_fv_compensation =
				info->pmic_wls_fv_compensation;
	}

	ret = strategy_class_fg_ops_get_temperature(&batt_temp);
	if (ret)
		return ret;
	batt_temp = batt_temp / 10;

	if (batt_temp < info->bat_temp_fv_comp_cold_th &&
	    info->support_diff_temp_comp)
		compensation = info->pmic_fv_compensation_cold;
	else if (batt_temp > info->bat_temp_fv_comp_hot_th &&
		 info->support_diff_temp_comp)
		compensation = info->pmic_fv_compensation_hot;
	else
		compensation = info->pmic_fv_compensation;

	vterm = compensation + effective_result;

	if (info->support_pmic_vterm_dynamics_adjust) {
		int raw_temp = 0;

		if (!strategy_class_fg_get_fastcharge()) {
			vterm = effective_result;
		} else {
			ret = strategy_class_fg_ops_get_temperature(&raw_temp);
			if (ret)
				return ret;
			if (raw_temp >= VTERM_DYN_TEMP_WARM &&
			    raw_temp < VTERM_DYN_TEMP_MIDDLE)
				vterm = info->pmic_fv_compensation +
					effective_result;
			else if (raw_temp >= VTERM_DYN_TEMP_MIDDLE &&
				 raw_temp < VTERM_DYN_TEMP_HIGH)
				vterm = info->pmic_middle_fv_compensation +
					effective_result;
			else if (raw_temp >= VTERM_DYN_TEMP_HIGH)
				vterm = info->pmic_high_fv_compensation +
					effective_result;
		}
	}

	if (info->base_flip_same) {
		if (!strategy_class_fg_get_fastcharge())
			compensation =
				(batt_temp >= BASE_FLIP_VTERM_TEMP_HOT_LOW &&
				 batt_temp < BASE_FLIP_VTERM_TEMP_HOT_HIGH) ?
					BASE_FLIP_VTERM_COMP_HOT :
					0;
		else if (batt_temp < BASE_FLIP_VTERM_TEMP_FFC_TH)
			compensation = BASE_FLIP_VTERM_COMP_FFC;
		else
			compensation = BASE_FLIP_VTERM_COMP_FFC_HOT;

		info->pmic_fv_compensation = compensation;
		vterm = compensation + effective_result;
	}

	if (info->support_multi_buck)
		return strategy_buckchg_process_multi_vterm(info, vterm);

	return platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER,
							vterm);
}

static int
strategy_buckchg_process_multi_iterm(struct strategy_buckchg_dev *info,
				     int result)
{
	return -EOPNOTSUPP;
}

#define BASE_FLIP_PACK_ITERM_HIGH 1046
#define BASE_FLIP_PACK_ITERM_MID 598
#define BASE_FLIP_PACK_ITERM_LOW 187
#define BASE_FLIP_ITERM_TEMP_WARM_LOW 180
#define BASE_FLIP_ITERM_TEMP_WARM_HIGH 300
#define BASE_FLIP_ITERM_TEMP_HOT_HIGH 400
#define BASE_FLIP_ITERM_COMP_HOT 104
#define BASE_FLIP_ITERM_COMP_WARM 82
#define BASE_FLIP_ITERM_COMP_FFC 50
#define BASE_FLIP_ITERM_COMP_LOW_PACK 20
#define BASE_FLIP_ITERM_COMP_OTHER_PACK 14

static int
strategy_buckchg_base_flip_iterm_comp(struct strategy_buckchg_dev *info)
{
	int temp = 0;

	if (!strategy_class_fg_get_fastcharge())
		return info->parallel_iterm == BASE_FLIP_PACK_ITERM_LOW ?
			       BASE_FLIP_ITERM_COMP_LOW_PACK :
			       BASE_FLIP_ITERM_COMP_OTHER_PACK;

	(void)strategy_class_fg_ops_get_temperature(&temp);

	if (temp >= BASE_FLIP_ITERM_TEMP_WARM_HIGH &&
	    temp < BASE_FLIP_ITERM_TEMP_HOT_HIGH &&
	    info->parallel_iterm == BASE_FLIP_PACK_ITERM_HIGH)
		return BASE_FLIP_ITERM_COMP_HOT;

	if (temp >= BASE_FLIP_ITERM_TEMP_WARM_LOW &&
	    temp < BASE_FLIP_ITERM_TEMP_WARM_HIGH &&
	    info->parallel_iterm == BASE_FLIP_PACK_ITERM_MID)
		return BASE_FLIP_ITERM_COMP_WARM;

	return BASE_FLIP_ITERM_COMP_FFC;
}

#define BASE_FLIP_SAME_ITERM_TEMP_HIGH 349
#define BASE_FLIP_SAME_ITERM_COMP_NO_FFC 20
#define BASE_FLIP_SAME_ITERM_COMP_HOT 68
#define BASE_FLIP_SAME_ITERM_COMP_WARM 75

static int strategy_buckchg_base_flip_same_iterm_comp(void)
{
	int temp = 0;

	if (!strategy_class_fg_get_fastcharge())
		return BASE_FLIP_SAME_ITERM_COMP_NO_FFC;

	(void)strategy_class_fg_ops_get_temperature(&temp);

	return temp > BASE_FLIP_SAME_ITERM_TEMP_HIGH ?
		       BASE_FLIP_SAME_ITERM_COMP_HOT :
		       BASE_FLIP_SAME_ITERM_COMP_WARM;
}

static int strategy_buckchg_set_iterm(struct mca_votable *votable, void *data,
				      int effective_result,
				      const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -EINVAL;
	if (effective_result < 0 || effective_result > 30000)
		return -ERANGE;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	if (info->support_base_flip)
		info->pmic_iterm_compensation =
			strategy_buckchg_base_flip_iterm_comp(info);

	if (info->base_flip_same)
		info->pmic_iterm_compensation =
			strategy_buckchg_base_flip_same_iterm_comp();

	effective_result = info->pmic_iterm_compensation + effective_result;

	if (info->support_multi_buck)
		return strategy_buckchg_process_multi_iterm(info,
							    effective_result);

	return platform_class_buckchg_ops_set_term_curr(MAIN_BUCK_CHARGER,
							effective_result);
}

static int strategy_buckchg_set_5v_input(struct mca_votable *votable,
					 void *data, int effective_result,
					 const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || effective_result < 0 || effective_result > 30000)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->buck_5v_in, effective_result);
	if (READ_ONCE(info->proc_data.voltage) != STATEGY_CHARGE_VBUS_5V)
		return 0;
	return mca_vote(info->input_limit_voter, "volt_limit",
			effective_result != 0, effective_result);
}

static int strategy_buckchg_set_5v_ichg(struct mca_votable *votable, void *data,
					int effective_result,
					const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || effective_result < 0 || effective_result > 30000)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->buck_5v_ich, effective_result);
	if (READ_ONCE(info->proc_data.voltage) != STATEGY_CHARGE_VBUS_5V)
		return 0;
	return mca_vote(info->charge_limit_voter, "volt_thermal_limit",
			effective_result != 0, effective_result);
}

static int strategy_buckchg_set_9v_input(struct mca_votable *votable,
					 void *data, int effective_result,
					 const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || effective_result < 0 || effective_result > 30000)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->buck_9v_in, effective_result);
	if (READ_ONCE(info->proc_data.voltage) != STATEGY_CHARGE_VBUS_9V)
		return 0;
	return mca_vote(info->input_limit_voter, "volt_limit",
			effective_result != 0, effective_result);
}

static int strategy_buckchg_set_9v_ichg(struct mca_votable *votable, void *data,
					int effective_result,
					const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || effective_result < 0 || effective_result > 30000)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->buck_9v_ich, effective_result);
	if (READ_ONCE(info->proc_data.voltage) != STATEGY_CHARGE_VBUS_9V)
		return 0;
	return mca_vote(info->charge_limit_voter, "volt_thermal_limit",
			effective_result != 0, effective_result);
}

static void strategy_buckchg_destroy_voters(struct strategy_buckchg_dev *info)
{
	if (!IS_ERR_OR_NULL(info->buck_9v_ich_voter))
		mca_destroy_votable(info->buck_9v_ich_voter);
	info->buck_9v_ich_voter = NULL;
	if (!IS_ERR_OR_NULL(info->buck_9v_in_voter))
		mca_destroy_votable(info->buck_9v_in_voter);
	info->buck_9v_in_voter = NULL;
	if (!IS_ERR_OR_NULL(info->buck_5v_ich_voter))
		mca_destroy_votable(info->buck_5v_ich_voter);
	info->buck_5v_ich_voter = NULL;
	if (!IS_ERR_OR_NULL(info->buck_5v_in_voter))
		mca_destroy_votable(info->buck_5v_in_voter);
	info->buck_5v_in_voter = NULL;
	if (!IS_ERR_OR_NULL(info->input_voltage_voter))
		mca_destroy_votable(info->input_voltage_voter);
	info->input_voltage_voter = NULL;
	if (!IS_ERR_OR_NULL(info->input_suppend_voter))
		mca_destroy_votable(info->input_suppend_voter);
	info->input_suppend_voter = NULL;
	if (!IS_ERR_OR_NULL(info->vterm_voter))
		mca_destroy_votable(info->vterm_voter);
	info->vterm_voter = NULL;
	if (!IS_ERR_OR_NULL(info->iterm_voter))
		mca_destroy_votable(info->iterm_voter);
	info->iterm_voter = NULL;
	if (!IS_ERR_OR_NULL(info->chg_enable_voter))
		mca_destroy_votable(info->chg_enable_voter);
	info->chg_enable_voter = NULL;
	if (!IS_ERR_OR_NULL(info->charge_limit_voter))
		mca_destroy_votable(info->charge_limit_voter);
	info->charge_limit_voter = NULL;
	if (!IS_ERR_OR_NULL(info->input_limit_voter))
		mca_destroy_votable(info->input_limit_voter);
	info->input_limit_voter = NULL;
}

static int strategy_buckchg_init_voter(struct strategy_buckchg_dev *info)
{
	int ret;

	info->input_limit_voter = mca_create_votable(
		"buck_input", MCA_VOTE_MIN, strategy_buckchg_input_limit,
		STATEGY_INPUT_DEFAULT_VALUE, info);
	if (IS_ERR(info->input_limit_voter)) {
		ret = PTR_ERR(info->input_limit_voter);
		info->input_limit_voter = NULL;
		goto fail;
	}
	info->charge_limit_voter = mca_create_votable(
		"buck_charge_curr", MCA_VOTE_MIN, strategy_buckchg_charge_limit,
		STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->charge_limit_voter)) {
		ret = PTR_ERR(info->charge_limit_voter);
		info->charge_limit_voter = NULL;
		goto fail;
	}
	info->chg_enable_voter = mca_create_votable("chg_enable", MCA_VOTE_AND,
						    strategy_buckchg_enable,
						    STATEGY_CHARGE_DISENABLE,
						    info);
	if (IS_ERR(info->chg_enable_voter)) {
		ret = PTR_ERR(info->chg_enable_voter);
		info->chg_enable_voter = NULL;
		goto fail;
	}
	info->iterm_voter = mca_create_votable("term_curr", MCA_VOTE_MAX,
					       strategy_buckchg_set_iterm,
					       STATEGY_ITERM_DEFAULT_VALUE,
					       info);
	if (IS_ERR(info->iterm_voter)) {
		ret = PTR_ERR(info->iterm_voter);
		info->iterm_voter = NULL;
		goto fail;
	}
	info->vterm_voter = mca_create_votable("term_volt", MCA_VOTE_MIN,
					       strategy_buckchg_set_vterm,
					       STATEGY_VTERM_DEFAULT_VALUE,
					       info);
	if (IS_ERR(info->vterm_voter)) {
		ret = PTR_ERR(info->vterm_voter);
		info->vterm_voter = NULL;
		goto fail;
	}
	info->input_suppend_voter = mca_create_votable(
		"input_suspend", MCA_VOTE_OR, strategy_buckchg_input_suspend,
		STATEGY_CHARGE_INPUT_SUSPEND, info);
	if (IS_ERR(info->input_suppend_voter)) {
		ret = PTR_ERR(info->input_suppend_voter);
		info->input_suppend_voter = NULL;
		goto fail;
	}
	info->input_voltage_voter =
		mca_create_votable("input_voltage", MCA_VOTE_AND,
				   strategy_buckchg_input_voltage, 1, info);
	if (IS_ERR(info->input_voltage_voter)) {
		ret = PTR_ERR(info->input_voltage_voter);
		info->input_voltage_voter = NULL;
		goto fail;
	}
	info->buck_5v_in_voter = mca_create_votable(
		"buck_5v_in", MCA_VOTE_MIN, strategy_buckchg_set_5v_input,
		STATEGY_INPUT_5V_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_5v_in_voter)) {
		ret = PTR_ERR(info->buck_5v_in_voter);
		info->buck_5v_in_voter = NULL;
		goto fail;
	}
	info->buck_5v_ich_voter = mca_create_votable(
		"buck_5v_ich", MCA_VOTE_MIN, strategy_buckchg_set_5v_ichg,
		STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_5v_ich_voter)) {
		ret = PTR_ERR(info->buck_5v_ich_voter);
		info->buck_5v_ich_voter = NULL;
		goto fail;
	}
	info->buck_9v_in_voter = mca_create_votable(
		"buck_9v_in", MCA_VOTE_MIN, strategy_buckchg_set_9v_input,
		STATEGY_INPUT_9V_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_9v_in_voter)) {
		ret = PTR_ERR(info->buck_9v_in_voter);
		info->buck_9v_in_voter = NULL;
		goto fail;
	}
	info->buck_9v_ich_voter = mca_create_votable(
		"buck_9v_ich", MCA_VOTE_MIN, strategy_buckchg_set_9v_ichg,
		STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_9v_ich_voter)) {
		ret = PTR_ERR(info->buck_9v_ich_voter);
		info->buck_9v_ich_voter = NULL;
		goto fail;
	}
	return 0;
fail:
	strategy_buckchg_destroy_voters(info);
	return ret;
}

static void strategy_buckchg_start_charging(struct strategy_buckchg_dev *info)
{
	mca_log_info("start charging\n");
	info->monitor_active = true;
	strategy_buck_update_req_volt(info);
	cancel_delayed_work(&info->monitor_work);
	strategy_class_buckchg_ops_adc_enable(info, true);
	queue_delayed_work(info->wq, &info->monitor_work, 0);
}

static void
strategy_buckchg_reset_charge_para(struct strategy_buckchg_dev *info)
{
	memset(&info->proc_data, 0, sizeof(info->proc_data));
	memset(&info->pwr_cap, 0, sizeof(info->pwr_cap));
	info->proc_data.voltage = STATEGY_CHARGE_VBUS_5V;
	info->is_non_compliant_qc = false;
	info->non_compliant_run_once = false;
	info->vbat_ov_count = 0;
	info->non_compliant_count = 0;
	info->vbat_drop_count = 0;
	info->last_jeita_vterm = 0;
	info->soc_step = 0;
	mca_vote(info->input_voltage_voter, "non_compliant_qc", false, 0);
	mca_vote(info->input_limit_voter, "non_compliant_qc", false, 0);

	mca_vote(info->chg_enable_voter, "online", true,
		 STATEGY_CHARGE_DISENABLE);
	mca_vote(info->buck_5v_in_voter, "wire_chg_type", true,
		 STATEGY_INPUT_DEFAULT_VALUE);

	mca_vote(info->buck_9v_in_voter, "wire_chg_type", true,
		 STATEGY_INPUT_DEFAULT_VALUE);
	mca_vote(info->input_limit_voter, "icl_limit", true,
		 STATEGY_INPUT_DEFAULT_VALUE);

	mca_vote(info->buck_9v_in_voter, "pmic_inductor", false, 0);
	mca_vote(info->buck_5v_in_voter, "pmic_inductor", false, 0);

	mca_vote(info->buck_5v_ich_voter, "wire_chg_type", true,
		 STATEGY_INPUT_DEFAULT_VALUE);

	mca_vote(info->buck_9v_ich_voter, "wire_chg_type", true,
		 STATEGY_CHARGE_CURRENT_DEFAULT_VALUE);
	mca_vote(info->input_voltage_voter, "real_type", true, 0);
	mca_vote(info->input_voltage_voter, "eoc_5v", false, 0);
	mca_vote(info->chg_enable_voter, "vbat_ovp", false,
		 STATEGY_CHARGE_ENABLE);
	mca_vote(info->input_voltage_voter, "lpd", false, 0);
	cancel_delayed_work(&info->csd_pulse_process_work);
	mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
	mca_vote(info->charge_limit_voter, "qc_done", false, 0);
	mca_vote(info->chg_enable_voter, "csd_pulse", false,
		 STATEGY_CHARGE_DISENABLE);
	mca_vote(info->charge_limit_voter, "full_replug", false, 0);
	info->csd_flag = false;
}

static void
strategy_buckchg_clear_pmic_temp_term(struct strategy_buckchg_dev *info)
{
	if (!info->support_pmic_vterm_dynamics_adjust)
		return;

	mca_vote_override(info->vterm_voter, "pmic_temp_term", false, 0);
	mca_vote_override(info->input_limit_voter, "pmic_temp_term", false, 0);
}

static void strategy_buckchg_stop_charging(struct strategy_buckchg_dev *info)
{
	mca_log_info("stop charging\n");
	info->monitor_active = false;
	cancel_delayed_work(&info->monitor_work);
	cancel_delayed_work(&info->wls_revchg_monitor_work);
	cancel_delayed_work(&info->check_pd_secret_work);
	cancel_delayed_work(&info->sw_cv_work);
	cancel_delayed_work(&info->base_flip_sw_cv_work);
	strategy_buckchg_sw_cv_stop(info);
	strategy_buckchg_reset_charge_para(info);
	strategy_class_buckchg_ops_set_input_volt(
		info, STATEGY_CHARGE_INPUT_VOLT_DEFAULT);
	strategy_class_buckchg_ops_adc_enable(info, false);
	strategy_class_buckchg_ops_set_opt_fws(info,
					       STATEGY_CHARGE_FWS_DEFAULT);
	strategy_class_buckchg_ops_set_usb_aicl_cont_thd(
		info, STATEGY_CHARGE_AICL_TH_4P4V);

	strategy_buckchg_clear_pmic_temp_term(info);
	info->pmic_temp_term_cleared = false;

	info->aicl_thd = 0;
	info->pdo_nums = 0;
}

static void
strategy_buckchg_process_online_change(int value,
				       struct strategy_buckchg_dev *info)
{
	if (value == info->proc_data.online)
		return;

	mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
			       MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	info->proc_data.online = value;
	if (value) {
		if (info->support_base_flip) {
			bool master_err = false;
			bool slave_err = false;

			platform_fg_ops_get_error_state(FG_IC_MASTER,
							&master_err);
			platform_fg_ops_get_error_state(FG_IC_SLAVE,
							&slave_err);
			if (!master_err || slave_err)
				platform_class_loadsw_set_lowpower_mode(
					LOADSW_ROLE_MASTER, false);
		}
		strategy_buckchg_start_charging(info);
	} else {
		platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MASTER,
							true);
		info->pps_ptf = USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_SUPPORTED;
		strategy_buckchg_stop_charging(info);
		info->proc_data.charge_done_force_5v = false;
		info->verify_process_end = 0;
		info->cold_hot_zone_cnt = 0;
		if (info->base_flip_same) {
			bool cp_enabled = false;

			(void)platform_class_cp_set_busovp(
				CP_ROLE_MASTER, CP_BUSOVP_DEFAULT_MV);
			(void)platform_class_cp_get_charging_enabled(
				CP_ROLE_MASTER, &cp_enabled);
			mca_log_info("cp_enabled = %d in discharging\n",
				     cp_enabled);
		}
	}
}

#define PDO_9V_VOLATGE 9000
static void stategy_buckchg_is_pdo_9v(struct strategy_buckchg_dev *info)
{
	if (info->proc_data.real_type != XM_CHARGER_TYPE_PD &&
	    info->proc_data.real_type != XM_CHARGER_TYPE_PPS &&
	    info->proc_data.real_type != XM_CHARGER_TYPE_PD_VERIFY)
		return;

	if (info->pdo_nums == 1 && info->pwr_cap.nums > 1)
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_USB_STS_CHANGE, NULL);
	info->pdo_nums = info->pwr_cap.nums;

	if (info->pwr_cap.nums <= 0 ||
	    info->pwr_cap.nums > ARRAY_SIZE(info->pwr_cap.cap)) {
		info->proc_data.is_pd_9v = false;
		info->proc_data.curr_pd_pos = 0;
		mca_log_info("invalid pwr_cap.nums\n");
		return;
	}

	for (int i = 0; i < info->pwr_cap.nums; i++) {
		if (info->pwr_cap.cap[i].max_voltage ==
			    info->pwr_cap.cap[i].min_voltage &&
		    info->pwr_cap.cap[i].max_voltage == PDO_9V_VOLATGE) {
			info->proc_data.is_pd_9v = true;
			info->proc_data.curr_pd_pos = i;
			mca_log_info("pdo[%d] can support 9v: %d\n", i,
				     info->proc_data.is_pd_9v);
			return;
		}
	}

	info->proc_data.is_pd_9v = false;
	info->proc_data.curr_pd_pos = 0;
	return;
}

static void
strategy_buckchg_process_type_change(int value,
				     struct strategy_buckchg_dev *info)
{
	struct timespec64 ts;

	if (value == info->proc_data.real_type) {
		if (value == XM_CHARGER_TYPE_UNKNOW)
			strategy_buckchg_check_charger_change(info);
		return;
	}

	info->proc_data.real_type = value;
	strategy_buckchg_check_charger_change(info);

	if (value == XM_CHARGER_TYPE_UNKNOW)
		return;

	if (info->proc_data.real_type == XM_CHARGER_TYPE_PD ||
	    info->proc_data.real_type == XM_CHARGER_TYPE_PPS ||
	    info->proc_data.real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		protocol_class_get_adapter_power_cap(ADAPTER_PROTOCOL_PD,
						     &info->pwr_cap);
		protocol_class_pd_get_suspend_support_status(
			TYPEC_PORT_0, &info->proc_data.pdsuspendsupported);
	}

	if (info->is_eu_model &&
	    info->proc_data.real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		info->proc_data.eu_start_time = ktime_get_boottime();
		mca_log_info("is_eu_model for PPS eu_start_time = %lld\n",
			     info->proc_data.eu_start_time);
	}
	stategy_buckchg_is_pdo_9v(info);
	strategy_buck_update_req_volt(info);
	mod_delayed_work(info->wq, &info->monitor_work, 0);

	ktime_get_boottime_ts64(&ts);
	mca_log_info("verify_process_end: %d\n", info->verify_process_end);
	if (info->proc_data.real_type == XM_CHARGER_TYPE_PD) {
		if (!info->verify_process_end && (u64)ts.tv_sec > 60) {
			mca_vote(info->input_limit_voter, "icl_limit", true,
				 STATEGY_INPUT_DEFAULT_VALUE);
			queue_delayed_work(info->wq,
					   &info->check_pd_secret_work,
					   msecs_to_jiffies(10000));
		}
	} else {
		mca_vote(info->input_limit_voter, "icl_limit", false,
			 STATEGY_INPUT_DEFAULT_VALUE);
	}
}

static void
strategy_buckchg_process_antiburn_change(int value,
					 struct strategy_buckchg_dev *info)
{
	mca_log_info("value: %d\n", value);
	strategy_buckchg_set_charge_volt(info, STATEGY_CHARGE_VBUS_5V);
	if (value)
		strategy_buckchg_stop_charging(info);
	else if (info->proc_data.online)
		strategy_buckchg_start_charging(info);
}

static void
strategy_buckchg_process_batt_btb_change(int value,
					 struct strategy_buckchg_dev *info)
{
	if (value) {
		mca_vote(info->input_voltage_voter, "batt_miss", true, 0);
		mca_vote(info->input_limit_voter, "batt_miss", true,
			 STAEGY_BATT_MISS_ICL);
#ifndef CONFIG_FACTORY_BUILD
		mca_vote(info->vterm_voter, "batt_miss", true,
			 STAEGY_BATT_MISS_FV);
#endif
	} else {
		mca_vote(info->input_voltage_voter, "batt_miss", false, 0);
		mca_vote(info->input_limit_voter, "batt_miss", false,
			 STATEGY_CHARGE_CURRENT_DEFAULT_VALUE);
		mca_vote(info->vterm_voter, "batt_miss", false,
			 STATEGY_VTERM_DEFAULT_VALUE);
	}
}

static void strategy_buckchg_process_soc_limit_change_more(
	int enable, struct strategy_buckchg_dev *info)
{
	if (!enable) {
		info->soc_step = 0;
		mca_log_info("SOC limit released\n");
		mca_vote(info->charge_limit_voter, "soc_limit", false, 0);
		mca_vote(info->input_limit_voter, "soc_limit", false, 0);
		mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
	} else {
		mca_log_info("SOC limit triggered\n");
		if (info->soc_step < 10)
			info->soc_step++;
		mca_vote(info->charge_limit_voter, "soc_limit", true,
			 soc_limit_stepper_table[info->soc_step].fcc_value);
		mca_vote(info->input_limit_voter, "soc_limit", true,
			 soc_limit_stepper_table[info->soc_step].icl_value);
		if (info->soc_step != 10) {
			mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
			queue_delayed_work(info->wq,
					   &info->soc_limit_stepper_work,
					   msecs_to_jiffies(1000));
		} else {
			mca_vote(info->chg_enable_voter, "soc_limit", true, 0);
		}
	}
	mca_log_info("cur_step = %d\n", info->soc_step);
}

static void
strategy_buckchg_soc_limit_stepper_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, soc_limit_stepper_work.work);

	int enable = READ_ONCE(info->soc_limit_more_enable);

	if (!info->proc_data.online)
		enable = 0;
	if (info->support_charge_more)
		strategy_buckchg_process_soc_limit_change_more(enable, info);
	else
		strategy_buckchg_process_soc_limit_change(enable, info);
}

static void
strategy_buckchg_process_soc_limit_change(int value,
					  struct strategy_buckchg_dev *info)
{
	if (value) {
		// use vote to control charge enable/disable
		mca_vote(info->chg_enable_voter, "soc_limit", true,
			 STATEGY_CHARGE_DISENABLE);
		mca_vote(info->input_voltage_voter, "soc_limit", true, 0);
		mca_log_info("SOC limit triggered, stopping charge\n");
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
	} else {
		mca_vote(info->chg_enable_voter, "soc_limit", false,
			 STATEGY_CHARGE_ENABLE);
		mca_vote(info->input_voltage_voter, "soc_limit", false, 0);
		mca_log_info("SOC limit released, starting charge\n");
		if (info->proc_data.online)
			mod_delayed_work(info->wq, &info->monitor_work, 0);
	}
}

static void
strategy_buckchg_process_cap_change(struct strategy_buckchg_dev *info)
{
	if (info->proc_data.real_type == XM_CHARGER_TYPE_PD ||
	    info->proc_data.real_type == XM_CHARGER_TYPE_PPS ||
	    info->proc_data.real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		protocol_class_get_adapter_power_cap(ADAPTER_PROTOCOL_PD,
						     &info->pwr_cap);
		protocol_class_pd_get_suspend_support_status(
			TYPEC_PORT_0, &info->proc_data.pdsuspendsupported);
	}

	stategy_buckchg_is_pdo_9v(info);
	strategy_buck_update_req_volt(info);
	mod_delayed_work(info->wq, &info->monitor_work, 0);
}

static void
strategy_buckchg_process_csd_pulse(int value, struct strategy_buckchg_dev *info)
{
	int ffc_sts;

	ffc_sts = strategy_class_fg_get_fastcharge();

	mca_log_info("value: %d, ffc_sts: %d\n", value, ffc_sts);
	if (ffc_sts == true && value > 0)
		strategy_class_fg_set_fastcharge(false);

	if (value == 1 || value == 2) {
		mca_vote(info->charge_limit_voter, "csd_pulse", true, 1500);
		info->csd_flag = true;
		queue_delayed_work(info->wq, &info->csd_pulse_process_work,
				   msecs_to_jiffies(75000));
	} else if (value == 3) {
		mca_vote(info->charge_limit_voter, "csd_pulse", true, 1500);
	} else if (value == 0) {
		cancel_delayed_work(&info->csd_pulse_process_work);
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
		mca_vote(info->chg_enable_voter, "csd_pulse", false,
			 STATEGY_CHARGE_DISENABLE);
		info->csd_flag = false;
	}
}

static int
strategy_buckchg_check_reverse_quick_charge(struct strategy_buckchg_dev *info,
					    int *out)
{
	int batt_temp = 0;
	int soc;
	bool otg = false;
	int eligible = 0;

	strategy_class_fg_ops_get_temperature(&batt_temp);
	soc = strategy_class_fg_ops_get_soc();
	batt_temp /= 10;
	protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &otg);
	mca_log_info(
		"otg_present = %d, batt_temp = %d, thermal_board_temp =%d\n",
		otg, batt_temp, info->thermal_board_temp);
	if (otg && !info->reverse_qc_gate0 && !info->reverse_qc_gate1 &&
	    batt_temp >= 0 && info->thermal_board_temp <= 400) {
		int soc_th = info->reverse_auth ? 29 : 69;

		eligible = (soc > soc_th);
	}
	*out = eligible;
	return 0;
}

static void
strategy_buckchg_cp_revert_handler(int auth_pos,
				   struct strategy_buckchg_dev *info)
{
	bool otg_present = false;
	int otg_boost_disable;
	int otg_enable;
	int pos;

	otg_boost_disable = (auth_pos >> 24) & 0xff;
	info->cp_revert_auth = (auth_pos >> 16) & 0xff;
	info->reverse_auth = (auth_pos & 0xff00) != 0;
	pos = auth_pos & 0xff;

	if (otg_boost_disable)
		otg_present = false;
	else
		protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0,
							&otg_present);

	platform_class_buckchg_ops_get_otg_boost_src(MAIN_BUCK_CHARGER,
						     &info->otg_boost_src);

	if (info->cp_revert_auth && otg_present) {
		char buf[128];
		int len;
		int rqc = 0;
		struct mca_event_notify_data n;

		strategy_buckchg_check_reverse_quick_charge(info, &rqc);
		len = snprintf(buf, sizeof(buf),
			       "POWER_SUPPLY_REVERSE_QUICK_CHARGE=%d", rqc);
		n.event = buf;
		n.event_len = len;
		mca_event_report_uevent(&n);
		mca_log_info("status: %d\n", rqc);
	}

	mca_log_err("otg_present: %d, last_otg_present: %d\n", otg_present,
		    info->last_otg_present);
	mca_log_err("cp_revert_auth: %d, pos: %d, last_pos: %d\n",
		    info->cp_revert_auth, pos, info->last_pos);

	if (otg_present && info->last_pos == pos) {
		mca_log_err("same handler, ignore...");
		platform_class_cp_dump_register(CP_ROLE_MASTER);
		return;
	}

	if ((info->last_otg_present != otg_present) && otg_present)
		queue_delayed_work(
			info->wq, &info->source_status_monitor_work,
			msecs_to_jiffies(SOURCE_STATUS_MONITOR_INTERVAL));
	else if ((info->last_otg_present != otg_present) && !otg_present)
		cancel_delayed_work(&info->source_status_monitor_work);

	info->last_otg_present = otg_present;
	info->last_pos = pos;

	if (otg_present) {
		(void)platform_class_cp_get_chip_vendor(CP_ROLE_MASTER,
							&info->cp_chip_vendor);

		if (pos == 1 && info->otg_boost_src) {
			mca_log_err("start external boost");

			// enable external boost
			otg_enable = 1;
			otg_enable = (0 << 16) | (2 << 8) | otg_enable;
			platform_class_buckchg_ops_set_boost_enable(
				MAIN_BUCK_CHARGER, otg_enable);

			// close revert cp boost
			msleep(400);
			platform_class_cp_set_charging_enable(CP_ROLE_MASTER,
							      false);
			platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, false);
			platform_class_cp_enable_wpcgate(CP_ROLE_MASTER, false);
			platform_class_cp_set_qb(CP_ROLE_MASTER, false);
			platform_class_cp_set_mode(CP_ROLE_MASTER,
						   CP_MODE_FORWARD_2_1);
		} else if (pos == 2) {
			mca_log_err("start revert 1_2 cp");

			if (info->cp_chip_vendor == CP_CHIP_VENDOR_BQ25960 &&
			    !info->bq_revchg_switch_flag) {
				mca_log_err(
					"entry bq25960 1_2 cp reverse mode");
				platform_class_cp_set_cp_reverse_mode(
					CP_ROLE_MASTER, true);
				info->bq_revchg_switch_flag = 1;
			} else {
				// enable revert cp boost
				platform_class_cp_enable_acdrv_manual(
					CP_ROLE_MASTER, true);
				platform_class_cp_set_adjustadble_timeout(
					CP_ROLE_MASTER, 0);
				platform_class_cp_set_mode(CP_ROLE_MASTER,
							   CP_MODE_REVERSE_1_2);
				platform_class_cp_enable_wpcgate(CP_ROLE_MASTER,
								 false);
				platform_class_cp_enable_ovpgate(CP_ROLE_MASTER,
								 true);
				platform_class_cp_set_qb(CP_ROLE_MASTER, true);
				if (info->cp_chip_vendor ==
				    CP_CHIP_VENDOR_SC8541) {
					mca_log_err(
						"entry sc8541 1_2 cp manual mode");
					platform_class_cp_set_manual_revchg_mode(
						CP_ROLE_MASTER, true);
				}
				platform_class_cp_set_charging_enable(
					CP_ROLE_MASTER, true);
			}

			// close external boost
			msleep(400);
			otg_enable = 0;
			otg_enable = (0 << 16) | (2 << 8) | otg_enable;
			platform_class_buckchg_ops_set_boost_enable(
				MAIN_BUCK_CHARGER, otg_enable);
		}
	} else {
		mca_log_err("end revert 1_2 cp");

		if (info->otg_boost_src == 0) {
			platform_class_cp_set_adjustadble_timeout(
				CP_ROLE_MASTER, CP_REVCHG_EXIT_TIMEOUT);
			if (info->cp_chip_vendor == CP_CHIP_VENDOR_SC8541) {
				mca_log_err("exit sc8541 1_2 cp manual mode");
				platform_class_cp_set_manual_revchg_mode(
					CP_ROLE_MASTER, false);
			}
			if (info->cp_chip_vendor == CP_CHIP_VENDOR_BQ25960 &&
			    info->bq_revchg_switch_flag == 1) {
				mca_log_err("exit bq25960 1_2 cp reverse mode");
				platform_class_cp_set_cp_reverse_mode(
					CP_ROLE_MASTER, false);
				info->bq_revchg_switch_flag = 0;
			}
		} else {
			platform_class_cp_set_charging_enable(CP_ROLE_MASTER,
							      false);
			platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, true);
			platform_class_cp_enable_wpcgate(CP_ROLE_MASTER, false);
			platform_class_cp_set_qb(CP_ROLE_MASTER, false);
			platform_class_cp_set_mode(CP_ROLE_MASTER,
						   CP_MODE_FORWARD_2_1);
		}

		// close otg
		msleep(400);
		otg_enable = 0;
		otg_enable = (0 << 16) | (2 << 8) | otg_enable;
		platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER,
							    otg_enable);

		platform_class_cp_set_adjustadble_timeout(CP_ROLE_MASTER, 40);
		info->last_otg_present = false;
		info->last_pos = 1;
		info->source_boost_status = 0;
	}
	platform_class_cp_dump_register(CP_ROLE_MASTER);
}

static void
strategy_buckchg_process_pmic_init_done(struct strategy_buckchg_dev *info)
{
	mca_log_info("deal with pmic_init_done\n");
	mca_rerun_election(info->chg_enable_voter);
	mca_rerun_election(info->input_limit_voter);
	mca_rerun_election(info->charge_limit_voter);
	mca_rerun_election(info->iterm_voter);
	mca_rerun_election(info->vterm_voter);
}

static int strategy_buckchg_handle_event(int event, int value, void *data)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_PMIC_INIT_DONE:
		strategy_buckchg_process_pmic_init_done(info);
		break;
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_USB_DISCONNECT:
		strategy_buckchg_process_online_change(value, info);
		break;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		strategy_buckchg_process_type_change(value, info);
		break;
	case MCA_EVENT_CHARGE_CAP_CHANGE:
		strategy_buckchg_process_cap_change(info);
		break;
	case MCA_EVENT_CONN_ANTIBURN_CHANGE:
		strategy_buckchg_process_antiburn_change(value, info);
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
		strategy_buckchg_process_batt_btb_change(value, info);
		break;
	case MCA_EVENT_BATT_AUTH_PASS:
		mca_log_err("receive batt_auth event, value %d", value);
		info->batt_auth = 1;
		mca_vote(info->input_voltage_voter, "batt_auth", false, 0);
		mca_vote(info->charge_limit_voter, "batt_auth", false,
			 info->chg_batt_auth_failed);
		if (info->proc_data.online)
			mod_delayed_work(info->wq, &info->monitor_work, 0);
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
		strategy_buckchg_stop_charging(info);
		break;
	case MCA_EVENT_CHARGE_RESTORE:
		(void)platform_class_buckchg_ops_get_online(
			MAIN_BUCK_CHARGER, &info->proc_data.online);
		if (info->proc_data.online)
			strategy_buckchg_start_charging(info);
		break;
	case MCA_EVENT_LPD_STATUS_CHANGE:
		if (value)
			mca_vote(info->input_voltage_voter, "lpd", true, 0);
		else
			mca_vote(info->input_voltage_voter, "lpd", false, 0);
		break;
	case MCA_EVENT_WIRELESS_MAGNETIC_QUIT_QC:
	case MCA_EVENT_WIRELESS_AUDIO_PHONE_STS:
		info->quit_qc = !!value;
		break;
	case MCA_EVENT_WIRELESS_REVCHG:
		mca_log_info("wireless revchg event %d\n", value);
		info->wls_revchg_en = value;
		if (value)
			mod_delayed_work(info->wq,
					 &info->wls_revchg_monitor_work, 0);
		else {
			cancel_delayed_work(&info->wls_revchg_monitor_work);
			strategy_buckchg_exit_wireless_revchg(info);
		}
		break;
	case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
		mca_log_info("receive pd verify process end event, value %d\n",
			     value);
		info->verify_process_end = value;
		if (info->verify_process_end)
			mca_vote(info->input_limit_voter, "icl_limit", false,
				 STATEGY_INPUT_DEFAULT_VALUE);
		break;
	case MCA_EVENT_CC_SHORT_VBUS:
		if (value)
			mca_vote(info->input_voltage_voter, "cc_short_vbus",
				 true, !value);
		else
			mca_vote(info->input_voltage_voter, "cc_short_vbus",
				 false, !value);
		break;
	case MCA_EVENT_VBAT_OVP_CHANGE:
		if (value)
			mca_vote(info->chg_enable_voter, "vbat_ovp", true,
				 STATEGY_CHARGE_DISENABLE);
		else
			mca_vote(info->chg_enable_voter, "vbat_ovp", false,
				 STATEGY_CHARGE_ENABLE);
		break;
	case MCA_EVENT_CP_CBOOT_FAIL:
		if (value) {
			mca_vote(info->input_voltage_voter, "cp_cboot_short",
				 true, !value);
			(void)platform_class_buckchg_ops_get_online(
				MAIN_BUCK_CHARGER, &info->proc_data.online);
			if (info->proc_data.online)
				strategy_buckchg_start_charging(info);
		} else {
			mca_vote(info->input_voltage_voter, "cp_cboot_short",
				 false, !value);
		}
		break;
	case MCA_EVENT_PPS_PTF:
		info->pps_ptf = value;
		mca_log_info("set PPS_PTF %d\n", info->pps_ptf);
		if (info->pps_ptf == USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_OVERTEMP)
			mca_vote(info->input_voltage_voter, "ptf", true, 1);
		break;
	case MCA_EVENT_IS_EU_MODEL:
		info->is_eu_model = value;
		mca_log_err("set buck is_eu_model %d\n", value);
		platform_class_buckchg_ops_set_eu_model(MAIN_BUCK_CHARGER,
							value);
		break;
	case MCA_EVENT_PLATE_SHOCK:
		if (value)
			mca_vote(info->input_limit_voter, "plate_shock", true,
				 STAEGY_CHARGE_PLATE_SHOCK);
		else
			mca_vote(info->input_limit_voter, "plate_shock", false,
				 0);
		break;
	case MCA_EVENT_CSD_SEND_PULSE:
		strategy_buckchg_process_csd_pulse(value, info);
		break;
	case MCA_EVENT_CP_REVERT_CHANGE:
		if (info->support_reverse_quick_charge)
			strategy_buckchg_cp_revert_handler(value, info);
		break;
	case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
		mca_log_info("memory_test enable soc_limit: %d\n", value);
		if (value) {
			info->soc_limit_low = 1;
			info->soc_limit_high = 3;
		} else {
			info->soc_limit_low = 0;
			info->soc_limit_high = 0;
		}
		break;
	case MCA_EVENT_PARALLEL_ITERM:
		info->parallel_iterm = value;
		mca_log_info("parallel_iterm: %d\n", info->parallel_iterm);
		break;
	case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
		info->soc_limit_low = value >> 8;
		info->soc_limit_high = value & 0xff;
		mca_log_info("debug_ctrl set soc_limit: %d %d\n",
			     info->soc_limit_low, info->soc_limit_high);
		break;
	default:
		break;
	}

	return 0;
}

static int strategy_buckchg_get_status(int status, void *value, void *data)
{
	struct strategy_buckchg_dev *info = data;
	int *cur_val = value;

	if (!info || !value)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	/* Wired policy calls this while holding its own mutex. */
	switch (status) {
	case STRATEGY_STATUS_TYPE_ONLINE:
		return platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER,
							     cur_val);
	case STRATEGY_STATUS_TYPE_CHARGING:
		*cur_val = READ_ONCE(info->strategy_init_done);
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*cur_val = READ_ONCE(info->proc_data.qc_type);
		break;
	case STRATEGY_STATUS_TYPE_ENABLE:
		*cur_val = mca_get_effective_result(info->chg_enable_voter);
		break;
	case STRATEGY_STATUS_TYPE_VBUS:
		*cur_val = READ_ONCE(info->proc_data.vbus);
		break;
	case STRATEGY_STATUS_TYPE_IBUS:
		*cur_val = READ_ONCE(info->proc_data.ibus);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#define USB_ICL_UNENUMERATED 100
static int
strategy_buckchg_check_charger_change(struct strategy_buckchg_dev *info)
{
#ifdef CONFIG_FACTORY_BUILD
	return 0;
#endif
	int charge_boot_mode = mca_log_get_charge_boot_mode();

	/* Release USB related vote if USB detach has been detected */
	if (info->proc_data.real_type == XM_CHARGER_TYPE_UNKNOW) {
		mca_vote(info->input_limit_voter, "usbicl", false, 0);
		mca_vote(info->input_limit_voter, "sdpicl", false, 0);
		info->sdp_vote_completed = false;
		mca_log_info("charger have been removed reset vote");
		return 0;
	}

	if (charge_boot_mode || !info->is_eu_model) {
		mca_log_info(
			" charge_boot_mode[%d] cancel sdp enumerated process",
			charge_boot_mode);
		return 0;
	}

	if (info->proc_data.real_type == XM_CHARGER_TYPE_SDP &&
	    !info->sdp_vote_completed) {
		mca_vote(info->input_limit_voter, "sdpicl", true,
			 USB_ICL_UNENUMERATED);
		info->sdp_vote_completed = true;
		mca_log_info("sdp charger unenumerated");
	}

	if (info->proc_data.real_type != XM_CHARGER_TYPE_SDP &&
	    info->sdp_vote_completed) {
		mca_vote(info->input_limit_voter, "sdpicl", false, 0);
		mca_vote(info->input_limit_voter, "usbicl", false, 0);
		info->sdp_vote_completed = false;
		mca_log_info("sdp charger error detected reset vote%d\n",
			     info->proc_data.real_type);
	}

	return 0;
}

#define USB_SUSPEND_ICL 0
#define USB_UNSUSPEND_ICL -1
#define USB_SUSPEND_ICL_DEFAULT 2
static int stategy_buckchg_set_usb_icl(int value,
				       struct strategy_buckchg_dev *info)
{
#ifdef CONFIG_FACTORY_BUILD
	return 0;
#endif
	int icl_ma = value / 1000; //ua switch ma;
	int charge_boot_mode = mca_log_get_charge_boot_mode();

	if (charge_boot_mode || !info->is_eu_model) {
		mca_log_info(
			"charge_boot_mode[%d] is_eu_model[%d] exit usb_icl",
			charge_boot_mode, info->is_eu_model);
		return 0;
	}
	mca_log_info("usb type[%d]icl_ma[%d]", info->proc_data.real_type,
		     icl_ma);
	if (info->proc_data.real_type != XM_CHARGER_TYPE_SDP) {
		if (info->proc_data.real_type == XM_CHARGER_TYPE_PD ||
		    info->proc_data.real_type == XM_CHARGER_TYPE_PPS) {
			if (!info->proc_data.pdsuspendsupported) {
				mca_log_info("USB suspend is not supported");
				return 0;
			} else {
				if (icl_ma == USB_SUSPEND_ICL_DEFAULT) {
					icl_ma = 0;
					mca_vote(info->input_limit_voter,
						 "usbicl", true, icl_ma);
					mca_log_info("USB suspend for pd");
				} else if (icl_ma == 900 || icl_ma == 500 ||
					   icl_ma == 100) {
					mca_vote(info->input_limit_voter,
						 "usbicl", false, 0);
					mca_log_info("USB Unsuspend for pd");
				} else
					mca_log_info(
						"invalid ICL value for pd %d",
						icl_ma);
				return 0;
			}
		} else {
			mca_log_info(
				"ICL setting is not allowed for usb type[%d]",
				info->proc_data.real_type);
			return 0;
		}
	}

	if (icl_ma == USB_SUSPEND_ICL_DEFAULT) {
		mca_log_info("USB Input Suspended");
		mca_vote(info->input_limit_voter, "usbicl", true, 0);
		info->usb_suspend = true;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
				       MCA_EVENT_USB_SUSPEND,
				       &info->usb_suspend);
	} else if (icl_ma == USB_UNSUSPEND_ICL) {
		mca_log_info("USB Input UnSuspended");
		mca_vote(info->input_limit_voter, "usbicl", false, 0);
		if (info->usb_suspend) {
			info->usb_suspend = false;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
					       MCA_EVENT_USB_SUSPEND,
					       &info->usb_suspend);
		}
	} else {
		if (info->proc_data.real_type == XM_CHARGER_TYPE_SDP &&
		    (icl_ma == 900 || icl_ma == 500)) {
			mca_vote(info->input_limit_voter, "sdpicl", true,
				 icl_ma);
			mca_vote(info->input_limit_voter, "usbicl", true,
				 icl_ma);
			mca_log_info("ICL for SDP set by HLOS is %d mA",
				     icl_ma);
		} else {
			mca_log_info("Invalid ICL for SDP set by HLOS is %d mA",
				     icl_ma);
		}

		if (info->usb_suspend) {
			info->usb_suspend = false;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
					       MCA_EVENT_USB_SUSPEND,
					       &info->usb_suspend);
		}
	}

	return 0;
}
static int strategy_buckchg_set_config(int config, int value, void *data)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || config != STRATEGY_CONFIG_INPUT_CURRENT_LIMIT ||
	    value < -1000 || value > 30000000)
		return -EINVAL;
	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	return stategy_buckchg_set_usb_icl(value, info);
}

static void strategy_buckchg_update_aicl_cfg(struct strategy_buckchg_dev *info)
{
	int aicl_thd = STATEGY_CHARGE_AICL_TH_4P4V;
	int reg_aicl_thd = 0;

	if (info->proc_data.chg_status == MCA_BUCK_CHG_NO_CHARGING ||
	    info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGE_DONE) {
		aicl_thd = STATEGY_CHARGE_AICL_TH_4P1V;
		mca_log_info(
			"discharging or done, should set aicl vth to 4.1V\n");
		goto update_aicl_cfg;
	}

	if (info->proc_data.real_type >= XM_CHARGER_TYPE_HVDCP3 &&
	    info->proc_data.real_type <= XM_CHARGER_TYPE_HVDCP3P5)
		return;

	if (info->proc_data.vbat >= STATEGY_CHARGE_AICL_VBAT_TH)
		aicl_thd = STATEGY_CHARGE_AICL_TH_4P5V;

#ifdef CONFIG_FACTORY_BUILD
	aicl_thd = STATEGY_CHARGE_AICL_TH_4P1V;
#endif

update_aicl_cfg:
	platform_class_buckchg_ops_get_usb_aicl_cont_thd(MAIN_BUCK_CHARGER,
							 &reg_aicl_thd);
	if (aicl_thd != info->aicl_thd || aicl_thd != reg_aicl_thd) {
		mca_log_info("vbat: %d, aicl_thd: %d, reg_aicl_thd: %d\n",
			     info->proc_data.vbat, aicl_thd, reg_aicl_thd);
		info->aicl_thd = aicl_thd;
		strategy_class_buckchg_ops_set_usb_aicl_cont_thd(info,
								 aicl_thd);
	}
}

static void
strategy_buckchg_select_charg_para(struct strategy_buckchg_dev *info)
{
	int ibus_limit = CHARGE_SDP_INPUT_DEFAULT;
	int ibat_limit = CHARGE_SDP_CHARGE_DEFAULT;
	int real_type = info->proc_data.real_type;
#ifdef CONFIG_FACTORY_BUILD
	bool cc_attach = false;
#endif

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_9V &&
	    real_type >= XM_CHARGER_TYPE_HVDCP2 &&
	    real_type <= XM_CHARGER_TYPE_HVDCP3P5)
		real_type = XM_CHARGER_TYPE_DCP;

	if (info->base_flip_same && real_type == XM_CHARGER_TYPE_FLOAT) {
		bool cp_enabled = false;

		(void)platform_class_cp_set_busovp(CP_ROLE_MASTER,
						   CP_BUSOVP_FLOAT_MV);
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER,
							     &cp_enabled);
		mca_log_info("cp_enabled = %d \n", cp_enabled);
	}

#ifdef CONFIG_FACTORY_BUILD
	protocol_class_pd_get_cc_status(TYPEC_PORT_0, &cc_attach);
	if (!cc_attach && (real_type == XM_CHARGER_TYPE_SDP ||
			   real_type == XM_CHARGER_TYPE_FLOAT))
		real_type = XM_CHARGER_TYPE_DCP;
	mca_log_err("cc_attach %d real_type %d\n", cc_attach, real_type);
#endif

	switch (real_type) {
	case XM_CHARGER_TYPE_UNKNOW:
		ibus_limit = 100;
		ibat_limit = 100;
		break;
	case XM_CHARGER_TYPE_SDP:
#ifdef CONFIG_FACTORY_BUILD
		ibus_limit = 600;
		ibat_limit = 600;
#else
		if (info->is_eu_model) {
			ibus_limit = info->in_sdp - 50;
		} else {
			ibus_limit = info->in_sdp;
		}
		ibat_limit = info->chg_sdp;
#endif
		break;
	case XM_CHARGER_TYPE_CDP:
		ibus_limit = info->in_cdp;
		ibat_limit = info->chg_cdp;
		break;
	case XM_CHARGER_TYPE_FLOAT:
	case XM_CHARGER_TYPE_OCP:
		ibus_limit = info->in_float;
		ibat_limit = info->chg_float;
		break;
	case XM_CHARGER_TYPE_DCP:
		if (READ_ONCE(info->smartchg_data.pwr_boost_state)) {
			ibus_limit = CHARGE_DCP_INPUT_BOOST;
			ibat_limit = CHARGE_DCP_CHARGE_BOOST;
		} else {
			ibus_limit = info->in_dcp;
			ibat_limit = info->chg_dcp;
		}
		break;
	case XM_CHARGER_TYPE_HVDCP2:
		ibus_limit = info->in_hvdcp;
		ibat_limit = info->chg_hvdcp;
		break;
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
		ibus_limit = info->in_hvdcp3;
		ibat_limit = info->chg_hvdcp3;
		break;
	case XM_CHARGER_TYPE_HVDCP3P5:
		ibus_limit = info->in_hvdcp3p5;
		ibat_limit = info->chg_hvdcp3p5;
		break;
	case XM_CHARGER_TYPE_TYPEC:
		ibus_limit = info->in_dcp;
		ibat_limit = info->chg_dcp;
		break;
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (info->pps_ptf == USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_OVERTEMP) {
			ibus_limit = CHARGE_PPS_PTF_INPUT_DEFAULT;
			ibat_limit = CHARGE_PPS_PTF_CHARGE_DEFAULT;
		} else {
			unsigned int in_limit = info->in_pd;
			int chg_limit = info->chg_pd;

			if (info->base_flip_same &&
			    strategy_class_fg_get_fastcharge() &&
			    info->proc_data.real_type == XM_CHARGER_TYPE_PPS) {
				int batt_temp = 0, temp_offset_flag = 0;

				(void)strategy_class_fg_ops_get_temperature(
					&batt_temp);
				batt_temp /= 10;
				(void)strategy_class_fg_get_temp_offset_flag(
					&temp_offset_flag);
				(void)strategy_class_fg_ops_get_voltage(
					&info->proc_data.vbat);

				in_limit = info->in_pps;
				if (batt_temp > PPS_FFC_BOOST_TEMP_THR ||
				    (batt_temp >
					     PPS_FFC_BOOST_TEMP_OFFSET_THR &&
				     temp_offset_flag)) {
					chg_limit = info->chg_pps;
					if (info->proc_data.vbat >=
					    PPS_FFC_BOOST_VBAT_THR)
						in_limit = info->in_pps +
							   PPS_FFC_IBUS_BOOST;
				}
				mca_log_info("iin_pd = %d, ichg_pd = %d\n",
					     in_limit, chg_limit);
			}

			ibus_limit =
				min(in_limit,
				    (unsigned int)info->pwr_cap
					    .cap[info->proc_data.curr_pd_pos]
					    .max_current);
			ibat_limit = chg_limit;
			if (info->pwr_cap.cap[info->proc_data.curr_pd_pos]
				    .max_current == 0) {
				ibus_limit =
					min(in_limit, DEFAULT_PD_CURRENT_MA);
				mca_log_info("pdo broadcast abnormal %d \n",
					     info->proc_data.curr_pd_pos);
			}
		}
		break;
	default:
		ibus_limit = info->in_sdp;
		ibat_limit = info->chg_sdp;
		break;
	};

	if (info->soc_limit_more_enable)
		ibus_limit = info->batt_type ? SOC_LIMIT_ICL_BATT_TYPE :
					       SOC_LIMIT_ICL_DEFAULT;

	if (ibus_limit != info->proc_data.ibus_limit) {
		mca_vote(info->buck_5v_in_voter, "wire_chg_type", true,
			 ibus_limit);
		mca_vote(info->buck_9v_in_voter, "wire_chg_type", true,
			 ibus_limit);
		mca_vote(info->input_limit_voter, "subpmic_hw", false,
			 0); //avoid vote default vaule
		info->proc_data.ibus_limit = ibus_limit;
		mca_log_info("set ibus_limit = %d\n", ibus_limit);
	}

	if (ibat_limit != info->proc_data.ibat_limit) {
		mca_vote(info->buck_5v_ich_voter, "wire_chg_type", true,
			 ibat_limit);
		mca_vote(info->buck_9v_ich_voter, "wire_chg_type", true,
			 ibat_limit);
		mca_vote(info->charge_limit_voter, "subpmic_hw", false,
			 0); //avoid vote default vaule
		info->proc_data.ibat_limit = ibat_limit;
		mca_log_info("set ibat_limit = %d\n", ibat_limit);
	}

	mca_log_info("ibus_limit = %d, ibat_limit = %d\n", ibus_limit,
		     ibat_limit);
}

static void strategy_buckchg_set_charge_volt(struct strategy_buckchg_dev *info,
					     int target_volt)
{
	int volt = target_volt;

	if (!target_volt) {
		mca_log_info("target_volt = 0v is invalid\n");
		return;
	}

	switch (info->proc_data.real_type) {
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		platform_class_buckchg_ops_set_qc_volt(MAIN_BUCK_CHARGER, volt);
		break;
	case XM_CHARGER_TYPE_PD:
		if (info->pwr_cap.nums == 0) {
			mca_log_info("pwr_cap nums is null\n");
			return;
		}
		(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0, volt);
		break;
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (info->pwr_cap.nums == 0) {
			mca_log_info("pwr_cap nums is null\n");
			return;
		}
		if (volt == STATEGY_CHARGE_VBUS_5V ||
		    volt == STATEGY_CHARGE_VBUS_9V ||
		    volt == STATEGY_CHARGE_VBUS_12V) {
			(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0,
							       volt);
		} else {
			mca_log_info("set no fix pdo\n");
			protocol_class_set_adapter_volt_and_curr(
				ADAPTER_PROTOCOL_PPS, volt,
				CHARGE_PPS_INPUT_DEFAULT);
		}
		break;
	default:
		break;
	}
}

static void strategy_buck_update_req_volt(struct strategy_buckchg_dev *info)
{
	switch (info->proc_data.real_type) {
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		mca_vote(info->input_voltage_voter, "real_type", true, 1);
		break;
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (info->proc_data.is_pd_9v)
			mca_vote(info->input_voltage_voter, "real_type", true,
				 1);
		else
			mca_vote(info->input_voltage_voter, "real_type", true,
				 0);
		break;
	default:
		mca_vote(info->input_voltage_voter, "real_type", true, 0);
		break;
	}
	mca_log_info(
		"real_type = %d, input_voltage_voter effective_result: %d\n",
		info->proc_data.real_type,
		mca_get_effective_result(info->input_voltage_voter));
}

/*static int strategy_buckchg_check_online_msleep(int ms, struct strategy_buckchg_dev *info)
{
	int i, count;
	count = ms / 10;
	for (i = 0; i < count; i++) {
		if (!info->proc_data.online)
			return -1;
		usleep_range(9900, 11000);
	}
	return 0;
}*/

#define VBUS_9V_OVP_VOLTAGE 10000
#define VBUS_5V_OVP_VOLTAGE 7500
#define PMIC_INDUCTOR_SECURE_ICL 1000
static void
strategy_buckchg_check_charge_volt(struct strategy_buckchg_dev *info)
{
	int target_volt = info->proc_data.voltage;
	int vbus = 0;

	if (info->wls_revchg_en) {
		mca_log_info(
			"wireless reverse is charging, do not request volt\n");
		return;
	}

	if ((info->proc_data.real_type >= XM_CHARGER_TYPE_PD &&
	     info->proc_data.real_type <= XM_CHARGER_TYPE_PD_VERIFY) &&
	    !info->pwr_cap.nums) {
		mca_log_info("pd pwr_cap nums is null\n");
		return;
	}

	platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
	vbus /= 1000;
	if ((target_volt == STATEGY_CHARGE_VBUS_9V &&
	     vbus < CHECK_VBUS_9V_HIGH_TH) ||
	    (target_volt == STATEGY_CHARGE_VBUS_5V &&
	     vbus > CHECK_VBUS_5V_LOW_TH)) {
		mca_log_info("target_volt: %d, vbus: %d\n", target_volt, vbus);
		strategy_buckchg_set_charge_volt(info, target_volt);
	}

	platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
	vbus /= 1000;
	if (target_volt == STATEGY_CHARGE_VBUS_9V &&
	    vbus >= VBUS_9V_OVP_VOLTAGE) {
		mca_vote(info->buck_9v_in_voter, "pmic_inductor", true,
			 PMIC_INDUCTOR_SECURE_ICL);
		mca_log_err("hvdcp_9v vbus_ovp decrease input current limit\n");
	} else {
		mca_vote(info->buck_9v_in_voter, "pmic_inductor", false, 0);
	}

	if (target_volt == STATEGY_CHARGE_VBUS_5V &&
	    vbus > VBUS_5V_OVP_VOLTAGE) {
		mca_vote(info->buck_5v_in_voter, "pmic_inductor", true,
			 PMIC_INDUCTOR_SECURE_ICL);
		mca_log_err("lvdcp_5v vbus_ovp decrease input current limit\n");
	} else {
		mca_vote(info->buck_5v_in_voter, "pmic_inductor", false, 0);
	}

	if (info->is_non_compliant_qc && !info->non_compliant_run_once) {
		mca_log_err("non_compliant_qc: target_volt: %d\n", target_volt);
		strategy_buckchg_set_charge_volt(info, target_volt);
		mca_vote(info->input_limit_voter, "non_compliant_qc", true,
			 1500);
		strategy_class_buckchg_ops_set_chg(info, false);
		strategy_class_buckchg_ops_set_chg(info, true);
		mca_log_err("restart aicl and recover buck charging\n");
		platform_class_buckchg_ops_set_rerun_aicl(MAIN_BUCK_CHARGER,
							  true);
		info->non_compliant_run_once = true;
	}
}

#define VBUS_QC_VOL_THRESHOLD_LOW 7200
#define NON_COMPLIANT_QC_IBUS_THR_LOW 200
static void strategy_buckchg_check_non_compliant_qc_charger(
	struct strategy_buckchg_dev *info)
{
	int target_volt = info->proc_data.voltage;
	int vbus = 0, ibus = 0;

	if (target_volt == STATEGY_CHARGE_VBUS_5V || info->is_non_compliant_qc)
		return;

	if (info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGE_DONE) {
		mca_log_info(
			"charge done, no need check non_compliant_qc_charger\n");
		return;
	}

	if (info->proc_data.real_type != XM_CHARGER_TYPE_HVDCP2 &&
	    info->proc_data.real_type != XM_CHARGER_TYPE_HVDCP3) {
		mca_log_info("only qc2/qc3 need check\n");
		return;
	}

	if (platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus))
		return;
	vbus /= 1000;
	if (platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER,
						    &info->proc_data.ibus))
		return;
	ibus = info->proc_data.ibus / 1000;
	if (target_volt == STATEGY_CHARGE_VBUS_9V &&
	    vbus < VBUS_QC_VOL_THRESHOLD_LOW &&
	    ibus < NON_COMPLIANT_QC_IBUS_THR_LOW) {
		mca_log_info("target_volt: %d, vbus: %d\n", target_volt, vbus);
		info->non_compliant_count++;
	} else {
		info->non_compliant_count = 0;
	}
	if (info->non_compliant_count >= 3 && !info->is_non_compliant_qc) {
		info->is_non_compliant_qc = true;
		info->non_compliant_count = 0;
		mca_log_err("qc is non-compliant, set request vol to 5V\n");
		mca_vote(info->input_voltage_voter, "non_compliant_qc", true,
			 0);
	}
}

static int
strategy_buckchg_update_charge_status(struct strategy_buckchg_dev *info)
{
	int main_chg_status = 0;
	int aux_chg_status = 0;
	int icl = 0;
	int ichg = 0;
	int suspend = 0;
	int charging_done = 0;
	int adsp_icl = 0;

	(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER,
							&main_chg_status);
	(void)platform_class_buckchg_ops_get_chg_status(AUX_BUCK_CHARGER,
							&aux_chg_status);
	(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER,
						      &info->proc_data.ibus);
	(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER,
						      &info->proc_data.vbus);
	(void)strategy_class_fg_ops_get_voltage(&info->proc_data.vbat);
	(void)platform_class_buckchg_ops_get_input_curr_lmt(MAIN_BUCK_CHARGER,
							    &adsp_icl);
	icl = mca_get_effective_result(info->input_limit_voter);
	ichg = mca_get_effective_result(info->charge_limit_voter);
	suspend = mca_get_effective_result(info->input_suppend_voter);

	switch (main_chg_status) {
	case MCA_BATT_CHGR_STATUS_INHIBIT:
	case MCA_BATT_CHGR_STATUS_TRICKLE:
	case MCA_BATT_CHGR_STATUS_PRECHARGE:
	case MCA_BATT_CHGR_STATUS_FULLON:
	case MCA_BATT_CHGR_STATUS_TAPER:
	case MCA_BATT_CHGR_STATUS_FAST_LINEAR:
		info->proc_data.chg_status = MCA_BUCK_CHG_STS_CHARGING;
		break;
	case MCA_BATT_CHGR_STATUS_TERMINATION:
		info->proc_data.chg_status = MCA_BUCK_CHG_STS_CHARGE_DONE;
		break;
	case MCA_BATT_CHGR_STATUS_PAUSE:
	case MCA_BATT_CHGR_STATUS_CHARGING_DISABLED:
		info->proc_data.chg_status = MCA_BUCK_CHG_NO_CHARGING;
		break;
	default:
		info->proc_data.chg_status = MCA_BUCK_CHG_STS_NA;
		break;
	}

	charging_done = strategy_class_fg_ops_get_charging_done();
	if (charging_done)
		info->proc_data.chg_status = MCA_BUCK_CHG_STS_CHARGE_DONE;

	mca_log_err(
		"pmic_chg_status: %d, chg_status: %d, chg_en: [%d][%s], chg_type: %d, vbat: %d, ibat: %d, vbus: %d, ibus: %d\n",
		main_chg_status, info->proc_data.chg_status,
		mca_get_effective_result(info->chg_enable_voter),
		mca_get_effective_client(info->chg_enable_voter),
		info->proc_data.real_type, info->proc_data.vbat,
		info->proc_data.ibat, info->proc_data.vbus,
		info->proc_data.ibus);
	mca_log_err(
		"ap_icl:[%s][%d], adsp_icl[%d], ichg:[%s][%d], input_suspend:[%s][%d]\n",
		mca_get_effective_client(info->input_limit_voter), icl,
		adsp_icl, mca_get_effective_client(info->charge_limit_voter),
		ichg, mca_get_effective_client(info->input_suppend_voter),
		suspend);
	return info->proc_data.chg_status;
}

#define FFC_START_BATT_SOC_THR 95

static void strategy_buckchg_ffc_pps_boost(struct strategy_buckchg_dev *info,
					   int iin, int ichg)
{
	struct mca_votable *ichg_voter;

	mca_log_info("iin_pd = %d, ichg_pd = %d\n", iin, ichg);

	if (info->proc_data.voltage == STATEGY_CHARGE_VBUS_5V) {
		mca_vote(info->buck_5v_in_voter, "wire_chg_type", true, iin);
		ichg_voter = info->buck_5v_ich_voter;
	} else {
		mca_vote(info->buck_9v_in_voter, "wire_chg_type", true, iin);
		ichg_voter = info->buck_9v_ich_voter;
	}
	mca_vote(ichg_voter, "wire_chg_type", true, ichg);
}

static void
strategy_buckchg_enable_fast_charge_mode(struct strategy_buckchg_dev *info,
					 int soc)
{
	int batt_temp = 0, fastcharge_mode = 0;
	int iterm = mca_get_effective_result(info->iterm_voter);
	int fcc = mca_get_effective_result(info->charge_limit_voter);

	if (!info->terminated_by_cp) {
		(void)strategy_class_fg_ops_get_temperature(&batt_temp);
		batt_temp /= 10;
		fastcharge_mode = strategy_class_fg_get_fastcharge();

		if (info->support_base_flip || info->base_flip_same) {
			iterm = info->parallel_iterm;
			mca_log_info("iterm: %d\n", iterm);

			if (info->base_flip_same && fastcharge_mode &&
			    info->proc_data.real_type == XM_CHARGER_TYPE_PPS) {
				int temp_offset_flag = 0;
				int in_limit;

				fcc = mca_get_effective_result(
					info->charge_limit_voter);
				(void)strategy_class_fg_get_temp_offset_flag(
					&temp_offset_flag);
				(void)strategy_class_fg_ops_get_voltage(
					&info->proc_data.vbat);
				if (iterm >= fcc &&
				    (batt_temp > PPS_FFC_BOOST_TEMP_THR ||
				     (batt_temp >
					      PPS_FFC_BOOST_TEMP_OFFSET_THR &&
				      temp_offset_flag))) {
					in_limit = info->in_pps;
					if (info->proc_data.vbat >=
					    PPS_FFC_BOOST_VBAT_THR)
						in_limit += PPS_FFC_IBUS_BOOST;
					strategy_buckchg_ffc_pps_boost(
						info, in_limit, info->chg_pps);
				}
			}
		}
	}

	mca_log_info("allow_start_ffc_batt_soc_thr: %d\n",
		     info->allow_start_ffc_batt_soc_thr);

	if (info->proc_data.real_type == XM_CHARGER_TYPE_PPS) {
		if (info->ffc_terminated_by_cp)
			(void)mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_QC_IBAT_MAX, &fcc);

		mca_log_info("batt_temp = %d, soc = %d, fcc = %d\n", batt_temp,
			     soc, fcc);

		if (soc < FFC_START_BATT_SOC_THR && !fastcharge_mode &&
		    batt_temp >= info->ffc_temp_low &&
		    batt_temp <= info->ffc_temp_high && info->batt_auth) {
			strategy_class_fg_set_fastcharge(true);
			mca_log_info("buck charger enable fast charge mode\n");
		} else if (batt_temp < info->ffc_temp_low ||
			   batt_temp > info->ffc_temp_high) {
			strategy_class_fg_set_fastcharge(false);
			mca_log_info("buck charger disable fast charge mode\n");
		} else if (fastcharge_mode &&
			   soc >= info->allow_start_ffc_batt_soc_thr &&
			   fcc <= iterm + info->curr_terminate_compensation) {
			if (info->support_base_flip || info->base_flip_same) {
				const char *client = mca_get_effective_client(
					info->charge_limit_voter);

				if (client && !strcmp(client, "fcc_limit")) {
					mca_log_info(
						"client_srt:%s is pmic + cp charging, skip\n",
						client);
					return;
				}
			}

			strategy_class_fg_set_fastcharge(false);
			mca_strategy_func_process(
				STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				MCA_EVENT_FCC_TOO_LOW, 0);
			mca_log_info(
				"buck charger disable fast charge mode by fcc too low\n");
		}
	}
}

static void strategy_buckchg_eoc_force_5v(struct strategy_buckchg_dev *info)
{
	mca_vote(info->input_voltage_voter, "eoc_5v", true, 0);
}

#define COLD_ZONE_LOW -100
#define COLD_ZONE_HIGH 50
#define HOT_ZONE_LOW 480
#define HOT_ZONE_HIGH 550
#define IS_BETWEEN(val, lval, rval)                       \
	((val >= lval) ? ((val <= rval) ? true : false) : \
			 ((val >= rval) ? true : false))
static int strategy_buckchg_charge_abnormal_cold_or_hot_zone(
	struct strategy_buckchg_dev *info)
{
	int icl = 0;
	int chg_en = 0;
	int temp = 0;
	int vterm = 0;

	strategy_class_fg_ops_get_temperature(&temp);
	if (!IS_BETWEEN(temp, COLD_ZONE_LOW, COLD_ZONE_HIGH) &&
	    !IS_BETWEEN(temp, HOT_ZONE_LOW, HOT_ZONE_HIGH)) {
		info->cold_hot_zone_cnt = 0;
		mca_log_info("temp: %d, not in cold or hot zone\n", temp);
		return 0;
	}

	icl = mca_get_effective_result(info->input_limit_voter);
	chg_en = mca_get_effective_result(info->chg_enable_voter);
	vterm = mca_get_effective_result(info->vterm_voter);
	if (!icl || !chg_en || info->proc_data.vbat >= vterm) {
		info->cold_hot_zone_cnt = 0;
		mca_log_info(
			"user can stop charging icl: %d, chg_en: %d, vterm: %d, vbat: %d, temp:%d\n",
			icl, chg_en, vterm, info->proc_data.vbat, temp);
		return 0;
	}

	if (info->proc_data.chg_status == MCA_BUCK_CHG_NO_CHARGING) {
		/* temp ranges aren't overlaping, so using the same count variable is safe */
		if (IS_BETWEEN(temp, COLD_ZONE_LOW, COLD_ZONE_HIGH)) {
			info->cold_hot_zone_cnt++;
			if (info->cold_hot_zone_cnt > 3) {
				info->cold_hot_zone_cnt = 0;
				mca_charge_mievent_report(
					CHARGE_DFX_LOW_TEMP_DISCHARGING, &temp,
					1);
			}
		} else if (IS_BETWEEN(temp, HOT_ZONE_LOW, HOT_ZONE_HIGH)) {
			info->cold_hot_zone_cnt++;
			if (info->cold_hot_zone_cnt > 3) {
				info->cold_hot_zone_cnt = 0;
				mca_charge_mievent_report(
					CHARGE_DFX_HIGH_TEMP_DISCHARGING, &temp,
					1);
			}
		} else {
			info->cold_hot_zone_cnt = 0;
		}
		mca_log_info("count: %d\n", info->cold_hot_zone_cnt);
	} else {
		info->cold_hot_zone_cnt = 0;
	}

	return 0;
}

#define STRATEGY_BUCKCHG_ENTER_QUICKCHG_TIME_MS 7000
#define VBAT_DROP_COUNT_TH 3

#define CP_TO_PMIC_DEFAULT_VTERM 4400
#define CP_TO_PMIC_VTERM_STEP 5
static void
strategy_buckchg_cp_to_pmic_decrease_vterm(struct strategy_buckchg_dev *info)
{
	int first_termination = 0;
	int taper_cp_to_pmic = 0;
	int target_vterm = 0;
	int jeita_vterm;

	if (!info->need_cp_to_pmic)
		return;
	if (info->proc_data.real_type != XM_CHARGER_TYPE_HVDCP3_B)
		return;

	strategy_class_fg_get_first_termination(&first_termination);
	mca_strategy_func_get_status(
		STRATEGY_FUNC_TYPE_QUICK_CHARGE,
		STRATEGY_STATUS_TYPE_QC_TAPER_DONE_NO_RETRY, &taper_cp_to_pmic);

	if (first_termination) {
		mca_vote(info->vterm_voter, "cp_to_pmic", false,
			 CP_TO_PMIC_DEFAULT_VTERM);
	} else {
		jeita_vterm = mca_get_client_vote(info->vterm_voter, "jeita");
		if (taper_cp_to_pmic && jeita_vterm != info->last_jeita_vterm) {
			target_vterm = jeita_vterm - CP_TO_PMIC_VTERM_STEP;
			mca_vote(info->vterm_voter, "cp_to_pmic", true,
				 target_vterm);
			info->last_jeita_vterm = jeita_vterm;
		}
	}

	mca_log_err(
		"first_termination_flag: %d, taper_cp_to_pmic: %d, target_vterm: %d\n",
		first_termination, taper_cp_to_pmic, target_vterm);
}

#define AICL_CONT_THD_DEFAULT 4100
static void
strategy_buckchg_update_aicl_restart(struct strategy_buckchg_dev *info)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int vbus;
	int thd = AICL_CONT_THD_DEFAULT;

	vbus = mca_get_effective_result(info->input_voltage_voter);
	if (vbus != info->aicl_vbus_cache) {
		mca_rerun_election(info->buck_5v_in_voter);
		mca_rerun_election(info->buck_9v_in_voter);
		info->aicl_vbus_cache = vbus;
		mca_log_info("input voltage %d, country %d, restart aicl\n",
			     vbus, hwid ? (int)hwid->country_version : -1);
		platform_class_buckchg_ops_set_restart_aicl(MAIN_BUCK_CHARGER,
							    true);
	}

	if (info->aicl_cont_thd_cache != thd) {
		platform_class_buckchg_ops_set_usb_aicl_cont_thd(
			MAIN_BUCK_CHARGER, thd);
		info->aicl_cont_thd_cache = thd;
	}
}

#define VUSB_OVP_LOCATION_BEFORE_CP 1
#define CP_PRESENT_RETRY_MAX 6
static void strategy_buckchg_check_cp_i2c_err(struct strategy_buckchg_dev *info)
{
	bool present = false;
	int ret;

	if (!info->vusb_ovp_location)
		return;

	ret = platform_class_cp_get_present(CP_ROLE_MASTER, &present);
	if (!ret && present) {
		info->cp_present_retry = 0;
		info->cp_i2c_err_voted = false;
		return;
	}

	if (info->cp_present_retry >= CP_PRESENT_RETRY_MAX) {
		if (info->cp_i2c_err_voted)
			return;
		if (info->vusb_ovp_location != VUSB_OVP_LOCATION_BEFORE_CP)
			return;
		mca_log_err("cp i2c error detected\n");
		mca_log_info(
			"vusb_ovp_location is BEFORE_CP, force input voltage\n");
		mca_vote(info->input_voltage_voter, "cp_i2c_error", true, 0);
		info->cp_i2c_err_voted = true;
	} else {
		info->cp_present_retry++;
		mca_log_info("CP not present, retrying... retry_count %d\n",
			     info->cp_present_retry);
	}
}

static void strategy_buckchg_debug_soc_limit(struct strategy_buckchg_dev *info,
					     int soc)
{
	int en = 0;

	if (info->soc_limit_low >= 1 && info->soc_limit_high >= 1 &&
	    info->soc_limit_low < info->soc_limit_high) {
		if (soc >= info->soc_limit_high)
			en = 1;
		else if (soc > info->soc_limit_low)
			return;
		else
			en = 0;
	} else {
		if (!mca_is_client_vote_enabled(info->chg_enable_voter,
						"debug_soc_limit"))
			return;
		en = 0;
	}

	mca_vote(info->chg_enable_voter, "debug_soc_limit", en, 0);
	mca_vote(info->input_limit_voter, "debug_soc_limit", en, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				  MCA_EVENT_DEBUG_CTRL_SOC_LIMIT, en);
}

static int strategy_buckchg_read_battery(struct strategy_buckchg_dev *info)
{
	int voltage, current_value, temperature, ret;

	ret = strategy_class_fg_ops_get_voltage(&voltage);
	if (ret)
		return ret;
	ret = strategy_class_fg_ops_get_current(&current_value);
	if (ret)
		return ret;
	ret = strategy_class_fg_ops_get_temperature(&temperature);
	if (ret)
		return ret;
	if (voltage < 2000 || voltage > 10000 || current_value < -50000000 ||
	    current_value > 50000000 || temperature < -500 ||
	    temperature > 1000)
		return -ERANGE;
	info->proc_data.vbat = voltage;
	info->proc_data.ibat = current_value;
	return 0;
}

static void strategy_buckchg_monitor_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, monitor_work.work);
	int interval = CHARGE_MONITOR_WORK_NORMAL_INTERVAL;
	int quick_charge_status = MCA_QUICK_CHG_STS_CHARGE_FAILED;
	int input_suspned = 0;
	int chg_en = 0, system_soc;
	int vterm = 0;
	int jeita_hot_result = 1;
	bool vbat_drop_exit_flag = false;

	system_soc = strategy_class_fg_ops_get_soc();
	if (system_soc < 0 || system_soc > 100 ||
	    strategy_buckchg_read_battery(info)) {
		mca_vote(info->chg_enable_voter, "sensor_error", true, 0);
		goto out;
	}
	mca_vote(info->chg_enable_voter, "sensor_error", false, 0);
	strategy_buckchg_debug_soc_limit(info, system_soc);
	strategy_buckchg_check_cp_i2c_err(info);
	if (system_soc <= ALLOW_QUICK_CHG_SOC_THR) {
		if (info->support_low_voltage_high_current_keep_buck_chg) {
			bool adc_enabled = false;
			u32 cp_vout = 0;

			(void)platform_class_cp_get_adc_enabled(CP_ROLE_MASTER,
								&adc_enabled);
			if (!adc_enabled) {
				(void)platform_class_cp_enable_adc(
					CP_ROLE_MASTER, true);
				mdelay(CP_ADC_SETTLE_MS);
			}
			(void)platform_class_cp_get_battery_vout(CP_ROLE_MASTER,
								 &cp_vout);
			if (cp_vout <= CP_KEEP_BUCK_VBAT_THR) {
				mca_log_err(
					"vout is: %d less than 3.4V, keep buck charge action\n",
					(int)cp_vout);
				goto qc_status;
			}
		}

		jeita_hot_result = mca_get_client_vote(info->chg_enable_voter,
						       "jeita-hot");
		mca_log_info("jeita_hot vote value: %d\n", jeita_hot_result);
		switch (info->proc_data.real_type) {
		case XM_CHARGER_TYPE_HVDCP3_B:
		case XM_CHARGER_TYPE_HVDCP3P5:
		case XM_CHARGER_TYPE_PD_VERIFY:
		case XM_CHARGER_TYPE_PPS:
			if (jeita_hot_result)
				mca_strategy_func_process(
					STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					MCA_EVENT_CHARGE_ACTION, 0);
			break;
		default:
			break;
		}

	} else {
		strategy_buckchg_enable_fast_charge_mode(info, system_soc);
	}

qc_status:
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					   STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
					   &quick_charge_status);
	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING) {
		info->cold_hot_zone_cnt = 0;
		goto out;
	}

	strategy_buckchg_check_charge_volt(info);
	strategy_buckchg_select_charg_para(info);
	strategy_buckchg_update_charge_status(info);
	strategy_buckchg_check_non_compliant_qc_charger(info);
	strategy_buckchg_update_aicl_cfg(info);
	strategy_buckchg_update_aicl_restart(info);
	strategy_buckchg_cp_to_pmic_decrease_vterm(info);
	if (info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGING) {
		if (info->support_pmic_vterm_dynamics_adjust &&
		    !info->pmic_temp_term_cleared) {
			strategy_buckchg_clear_pmic_temp_term(info);
			info->pmic_temp_term_cleared = true;
		}
		if (info->proc_data.charge_done_force_5v) {
			info->proc_data.charge_done_force_5v = false;
			mca_vote(info->input_voltage_voter, "eoc_5v", false, 0);
		}
		// WA: rerun aicl if ibus too low
		if (info->proc_data.ibus < 150000 &&
		    mca_get_effective_result(info->input_limit_voter) >= 500) {
			mca_log_info("ibus abnormally low: %d, rerun aicl\n",
				     info->proc_data.ibus);
			platform_class_buckchg_ops_set_rerun_aicl(
				MAIN_BUCK_CHARGER, true);
		}
	}
	if ((info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGE_DONE) &&
	    !info->proc_data.charge_done_force_5v) {
		strategy_buckchg_sw_cv_stop(info);
		strategy_buckchg_eoc_force_5v(info);
		info->proc_data.charge_done_force_5v = true;
	}
	if (!info->proc_data.chg_en) {
		mca_vote(info->chg_enable_voter, "online", true,
			 STATEGY_CHARGE_ENABLE);
		info->proc_data.chg_en = STATEGY_CHARGE_ENABLE;
	}

	// WA: solve pmic abnormal status
	vterm = mca_get_effective_result(info->vterm_voter);
	input_suspned = mca_get_effective_result(info->input_suppend_voter);
	chg_en = mca_get_effective_result(info->chg_enable_voter);
	if (vterm >= STATEGY_CHARGE_VTERM_LOW_TH && !input_suspned) {
		if (chg_en &&
		    info->proc_data.chg_status == MCA_BUCK_CHG_NO_CHARGING) {
			strategy_class_buckchg_ops_set_chg(info, false);
			strategy_class_buckchg_ops_set_chg(info, true);
			mca_log_err("recover buck charging\n");
		}
	}
	strategy_buckchg_charge_abnormal_cold_or_hot_zone(info);

	if (info->proc_data.vbat <= vterm - 20) {
		info->vbat_drop_count++;
	} else {
		info->vbat_drop_count = 0;
	}
	vbat_drop_exit_flag =
		info->vbat_drop_count >= VBAT_DROP_COUNT_TH ? true : false;
	info->vbat_drop_count = vbat_drop_exit_flag ? 0 : info->vbat_drop_count;

	if (info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGING &&
	    !info->sw_cv_running && vterm >= STATEGY_CHARGE_VTERM_LOW_TH &&
	    info->proc_data.vbat >= vterm - CHARGE_SW_CV_VBAT_ALARM_DELTA) {
		mca_log_err("vbat: %d, vterm: %d, start sw_cv_work\n",
			    info->proc_data.vbat, vterm);
		strategy_buckchg_sw_cv_start(info);
	} else if (info->sw_cv_running && (!chg_en || vbat_drop_exit_flag)) {
		mca_log_err(
			"vbat: %d, vterm: %d, chg_en: %d, stop sw_cv_work\n",
			info->proc_data.vbat, vterm, chg_en);
		strategy_buckchg_sw_cv_stop(info);
	}

	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGE_DONE)
		interval = CHARGE_MONITOR_WORK_FAST_INTERVAL;

out:
	queue_delayed_work(info->wq, &info->monitor_work,
			   msecs_to_jiffies(interval));
}

static void
strategy_buckchg_base_flip_sw_cv_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, base_flip_sw_cv_work.work);
	static const int sw_cv_volt_delta_map[][2] = { { 1000, 7 }, { 0, 2 } };
	int interval = CHARGE_SW_CV_WORK_NORMAL_INTERVAL;
	int vterm = mca_get_client_vote(info->vterm_voter, "jeita");
	int iterm = info->parallel_iterm;
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int first_termination = 0;
	int volt_delta = sw_cv_volt_delta_map[0][1];
	int vbat, ibat_ua, ibat, actual_vterm, fv_step, vbat_delta;
	bool fastcharge;
	int i;

	if (strategy_buckchg_read_battery(info)) {
		queue_delayed_work(info->wq, &info->base_flip_sw_cv_work,
			msecs_to_jiffies(CHARGE_SW_CV_WORK_NORMAL_INTERVAL));
		return;
	}
	fastcharge = strategy_class_fg_get_fastcharge();
	vbat = info->proc_data.vbat;
	ibat_ua = -info->proc_data.ibat;
	ibat = ibat_ua / 1000;

	(void)strategy_class_fg_get_first_termination(&first_termination);
	mca_log_info("first_termination_flag:%d", first_termination);

	actual_vterm = first_termination ?
			       mca_get_effective_result(info->vterm_voter) :
			       vterm;
	actual_vterm += info->pmic_fv_compensation;

	if (fastcharge && !info->base_flip_same) {
		vbat_delta = BASE_FLIP_SW_CV_VBAT_DELTA_FAST;
		fv_step = BASE_FLIP_SW_CV_FV_STEP_FAST;
	} else {
		vbat_delta = BASE_FLIP_SW_CV_VBAT_DELTA_SLOW;
		fv_step = BASE_FLIP_SW_CV_FV_STEP_SLOW;
	}

	mca_log_info(
		"vbat: %d, ibat: %d, vterm: %d, actual_vterm: %d, iterm: %d, fcc: %d, fast_charge: %d, fv_step: %d\n",
		vbat, ibat, vterm, actual_vterm, iterm, fcc, fastcharge,
		fv_step);

	for (i = 0;
	     i < sizeof(sw_cv_volt_delta_map) / sizeof(sw_cv_volt_delta_map[0]);
	     i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (fastcharge && vbat >= vterm - volt_delta) {
		interval = CHARGE_SW_CV_WORK_FAST_INTERVAL;
		if (iterm < ibat - BASE_FLIP_SW_CV_FCC_STEP) {
			if (fcc - ibat >= BASE_FLIP_SW_CV_FCC_BUFFER)
				mca_vote(info->charge_limit_voter, "sw_cv",
					 true,
					 ibat / BASE_FLIP_SW_CV_FCC_STEP *
						 BASE_FLIP_SW_CV_FCC_STEP);
			else if (info->base_flip_same && !first_termination &&
				 ibat - iterm > BASE_FLIP_SW_CV_ITERM_DELTA)
				mca_vote(info->charge_limit_voter, "sw_cv",
					 true, fcc - BASE_FLIP_SW_CV_FCC_STEP);
		}
	}

	if (vbat >= vterm - vbat_delta) {
		int steps;

		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n",
			    info->vbat_ov_count);
		++info->vbat_ov_count;
		steps = min(info->vbat_ov_count, BASE_FLIP_SW_CV_OV_MAX_STEP);
		platform_class_buckchg_ops_set_term_volt(
			MAIN_BUCK_CHARGER, actual_vterm - steps * fv_step);
	}

	queue_delayed_work(info->wq, &info->base_flip_sw_cv_work,
			   msecs_to_jiffies(interval));
}

static void strategy_buckchg_sw_cv_start(struct strategy_buckchg_dev *info)
{
	info->sw_cv_running = true;
	mca_vote(info->charge_limit_voter, "sw_cv", false, 0);
	/* a base-flip pack runs the base-flip variant of the work */
	queue_delayed_work(info->wq,
			   (info->support_base_flip || info->base_flip_same) ?
				   &info->base_flip_sw_cv_work :
				   &info->sw_cv_work,
			   msecs_to_jiffies(CHARGE_SW_CV_WORK_FAST_INTERVAL));
}

static void strategy_buckchg_sw_cv_stop(struct strategy_buckchg_dev *info)
{
	mca_log_err("chg_status: %d, stop sw_cv_work\n",
		    info->proc_data.chg_status);
	cancel_delayed_work(&info->sw_cv_work);
	cancel_delayed_work(&info->base_flip_sw_cv_work);
	mca_vote(info->charge_limit_voter, "sw_cv", false, 0);
	info->sw_cv_running = false;
	info->vbat_ov_count = 0;
}

static int strategy_buckchg_sw_cv_fv_comp(struct strategy_buckchg_dev *info)
{
	int temp = 0;

	if (!info->support_diff_temp_comp)
		return info->pmic_fv_compensation;

	if (!strategy_class_fg_get_fastcharge())
		return 0;

	strategy_class_fg_ops_get_temperature(&temp);
	if (temp >= SW_CV_FV_COMP_TEMP_LOW && temp < SW_CV_FV_COMP_TEMP_MID)
		return info->pmic_fv_compensation;
	if (temp >= SW_CV_FV_COMP_TEMP_MID && temp < SW_CV_FV_COMP_TEMP_HIGH)
		return info->pmic_middle_fv_compensation;
	if (temp >= SW_CV_FV_COMP_TEMP_HIGH && temp <= SW_CV_FV_COMP_TEMP_MAX)
		return info->pmic_high_fv_compensation;

	return 0;
}

static void strategy_buckchg_sw_cv_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, sw_cv_work.work);
	static const int sw_cv_volt_delta_map[][2] = { { 1000, 4 }, { 0, 2 } };
	int interval = CHARGE_SW_CV_WORK_NORMAL_INTERVAL;
	int vbat, ibat;
	int vterm = mca_get_effective_result(info->vterm_voter);
	int iterm = mca_get_effective_result(info->iterm_voter);
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int volt_delta = sw_cv_volt_delta_map[0][1];

	if (strategy_buckchg_read_battery(info)) {
		queue_delayed_work(info->wq, &info->sw_cv_work,
			msecs_to_jiffies(CHARGE_SW_CV_WORK_NORMAL_INTERVAL));
		return;
	}
	vbat = info->proc_data.vbat;
	ibat = -info->proc_data.ibat / 1000;
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, iterm: %d, fcc: %d\n",
		     vbat, ibat, vterm, iterm, fcc);

	for (int i = 0;
	     i < sizeof(sw_cv_volt_delta_map) / sizeof(sw_cv_volt_delta_map[0]);
	     i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (vbat >= vterm - volt_delta) {
		int fcc_step = info->sw_cv_fcc_step;

		interval = CHARGE_SW_CV_WORK_FAST_INTERVAL;
		if (ibat - fcc_step > iterm) {
			int target;

			if (fcc - ibat >= 2 * fcc_step)
				target = fcc_step ? ibat / fcc_step * fcc_step :
						    0;
			else
				target = fcc - fcc_step;
			if (target - iterm < info->sw_cv_fcc_limit_buffer)
				target = iterm + info->sw_cv_fcc_limit_buffer;
			mca_vote(info->charge_limit_voter, "sw_cv", true,
				 target);
		}
	} else {
		interval = CHARGE_SW_CV_WORK_NORMAL_INTERVAL;
	}

	if (vbat >= (vterm - 1)) {
		int fv_comp = strategy_buckchg_sw_cv_fv_comp(info);

		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n",
			    info->vbat_ov_count);
		++info->vbat_ov_count;
		if (info->vbat_ov_count == 1)
			platform_class_buckchg_ops_set_term_volt(
				MAIN_BUCK_CHARGER,
				vterm + fv_comp - info->sw_cv_fv_step);
		else if (info->vbat_ov_count >= 2)
			platform_class_buckchg_ops_set_term_volt(
				MAIN_BUCK_CHARGER,
				vterm + fv_comp - 2 * info->sw_cv_fv_step);
	}

	queue_delayed_work(info->wq, &info->sw_cv_work,
			   msecs_to_jiffies(interval));
}

#if IS_ENABLED(CONFIG_MCA_BUCK_WLS_REV_COORDINATION)
static int
strategy_buckchg_wireless_revchg_msleep(int ms,
					struct strategy_buckchg_dev *info)
{
	int i, count;

	count = ms / 10;

	for (i = 0; i < count; i++) {
		bool user_reverse_chg = false;
		(void)mca_wireless_rev_get_user_reverse_chg(&user_reverse_chg);
		if (!info->proc_data.online || !info->wls_revchg_en ||
		    !user_reverse_chg)
			return -1;
		usleep_range(9900, 11000);
	}

	return 0;
}

#endif

static void
strategy_buckchg_exit_wireless_revchg(struct strategy_buckchg_dev *info)
{
	mca_vote(info->input_limit_voter, "wireless_revchg", false, 0);
	info->proc_data.wls_revchg_init_done = false;
	info->rev_icl_for_qc2 = false;
}

#if IS_ENABLED(CONFIG_MCA_BUCK_WLS_REV_COORDINATION)
static int
strategy_buckchg_process_wireless_revchg(struct strategy_buckchg_dev *info)
{
	int real_type = info->proc_data.real_type;
	int vbus = 0, cnt = 0;
	int rev_req_vadp = 0, req_volt_valid_h = 0, req_volt_valid_l = 0;
	int ret = 0;

	if (real_type < XM_CHARGER_TYPE_DCP ||
	    real_type > XM_CHARGER_TYPE_PD_VERIFY)
		return -1;

	if (real_type == XM_CHARGER_TYPE_PD) {
		if (strategy_buckchg_wireless_revchg_msleep(
			    WLS_REVCHG_NORMAL_INTERVAL, info))
			return -1;
	} else if (real_type == XM_CHARGER_TYPE_DCP) {
		if (strategy_buckchg_wireless_revchg_msleep(
			    WLS_REVCHG_SLOW_INTERVAL, info))
			return -1;
	} else if (real_type == XM_CHARGER_TYPE_HVDCP3) {
		if (strategy_buckchg_wireless_revchg_msleep(
			    WLS_REVCHG_FAST_INTERVAL, info))
			return -1;
	}

	real_type = info->proc_data.real_type;
	mca_vote(info->input_limit_voter, "wireless_revchg", true,
		 CHARGE_WLS_REVCHG_INPUT_DEFAULT);
	if (real_type == XM_CHARGER_TYPE_PPS ||
	    real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		rev_req_vadp = info->rev_req_vadp[0];
		req_volt_valid_h = info->rev_vadp_valid_h[0] * 1000;
		req_volt_valid_l = info->rev_vadp_valid_l[0] * 1000;
	} else {
		rev_req_vadp = info->rev_req_vadp[1];
		req_volt_valid_h = info->rev_vadp_valid_h[1] * 1000;
		req_volt_valid_l = info->rev_vadp_valid_l[1] * 1000;
	}
	strategy_buckchg_set_charge_volt(info, rev_req_vadp);

	if (strategy_buckchg_wireless_revchg_msleep(300, info))
		return -1;
	(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
	mca_log_info(
		"chg_type: %d, req_vadp: %d, vbus: %d, rev_vadp_valid_h: %d, rev_vadp_valid_l: %d\n",
		real_type, rev_req_vadp, vbus, req_volt_valid_h,
		req_volt_valid_l);
	while (vbus < req_volt_valid_l || vbus > req_volt_valid_h) {
		if (cnt > 5)
			break;
		if (strategy_buckchg_wireless_revchg_msleep(300, info))
			return -1;
		(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER,
							      &vbus);
		cnt++;
	}
	if (cnt > 5) {
		mca_log_err("request 9V vbus fail\n");
		return -1;
	}

	info->proc_data.wls_revchg_init_done = true;
	ret = mca_wireless_rev_set_wired_chg_ok(true);
	return ret;
}

#endif

static void
strategy_wls_revchg_monitor_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info =
		container_of(work, struct strategy_buckchg_dev,
			     wls_revchg_monitor_work.work);
	int ret = 0;
	int interval = CHARGE_MONITOR_WORK_NORMAL_INTERVAL;

	if (!info->proc_data.wls_revchg_init_done) {
#if IS_ENABLED(CONFIG_MCA_BUCK_WLS_REV_COORDINATION)
		ret = strategy_buckchg_process_wireless_revchg(info);
#else
		ret = -EOPNOTSUPP;
#endif
	}
	if (ret || !info->wls_revchg_en)
		goto err_out;
	else {
		if (info->proc_data.real_type == XM_CHARGER_TYPE_HVDCP2 &&
		    !info->rev_icl_for_qc2) {
			mca_vote(info->input_limit_voter, "wireless_revchg",
				 true, CHARGE_WLS_REVCHG_INPUT_QC2);
			info->rev_icl_for_qc2 = true;
		}
		queue_delayed_work(info->wq, &info->wls_revchg_monitor_work,
				   msecs_to_jiffies(interval));
		return;
	}

err_out:
	strategy_buckchg_exit_wireless_revchg(info);
}

static void strategy_csd_pulse_process_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, csd_pulse_process_work.work);

	if (info->csd_flag) {
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
		mca_vote(info->chg_enable_voter, "csd_pulse", true,
			 STATEGY_CHARGE_DISENABLE);
		info->csd_flag = false;
		queue_delayed_work(info->wq, &info->csd_pulse_process_work,
				   msecs_to_jiffies(75000));
		mca_log_info("vote fcc and discharge logic\n");
	} else {
		mca_vote(info->chg_enable_voter, "csd_pulse", false,
			 STATEGY_CHARGE_DISENABLE);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					  MCA_EVENT_CSD_SEND_PULSE, 0);
		mca_log_info("resume charging logic\n");
	}
}

static void
strategy_source_status_monitor_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info =
		container_of(work, struct strategy_buckchg_dev,
			     source_status_monitor_work.work);
	int eligible = 0;
	int retry;
	int ibus = 0;
	int ret;
	int i;
	char buf[128];
	struct mca_event_notify_data n;

	strategy_buckchg_check_reverse_quick_charge(info, &eligible);

	if (info->support_revchg_screenon) {
		if (info->src_monitor_flag) {
			if (eligible == 1)
				eligible = 2;
			info->src_monitor_cnt = 0;
			goto commit;
		}
		if (eligible == 1) {
			if (info->src_monitor_cnt >= 2)
				goto reschedule;
			eligible = 2;
		}
		strategy_class_fg_ops_get_current(&info->src_monitor_cur);
		mca_log_info("source monitor current: %d\n",
			     info->src_monitor_cur);
		if (info->src_monitor_cur > 3000031)
			info->src_monitor_cnt = 0;
		else
			info->src_monitor_cnt += 1;
	}

commit:
	if (eligible == info->source_boost_status)
		goto reschedule;

	if (!info->support_revchg_screenon) {
		mca_log_err("source boost status: %d\n", eligible);
		info->source_boost_status = eligible;
		protocol_class_pd_set_gear_shift(TYPEC_PORT_0,
						 info->source_boost_status);
		goto reschedule;
	}

	if (info->src_gear_last != eligible) {
		mca_log_err("set gear shift: %d\n", eligible);
		protocol_class_pd_set_gear_shift(TYPEC_PORT_0, eligible);
		info->src_gear_last = eligible;
	}

	if (eligible == 2 && info->source_boost_status == 1) {
		info->src_monitor_byte = 0;
		retry = 5;
		do {
			platform_class_cp_enable_adc(CP_ROLE_MASTER, true);
			for (i = 0; i < 20; i++)
				udelay(1000);
			ret = platform_class_cp_get_bus_current(CP_ROLE_MASTER,
								&ibus);
			mca_log_err("cp bus current: %d, ret: %d\n", ibus, ret);
			if (ret || ibus > 1500)
				goto reschedule;
		} while (--retry);

		n.event_len = snprintf(buf, sizeof(buf),
				       "POWER_SUPPLY_REVERSE_QUICK_CHARGE=%d",
				       2);
		n.event = buf;
		mca_event_report_uevent(&n);
	}
	info->source_boost_status = eligible;

reschedule:
	queue_delayed_work(
		info->wq, &info->source_status_monitor_work,
		msecs_to_jiffies((info->support_revchg_screenon && eligible) ?
					 250 :
					 1250));
}

static void
strategy_buckchg_check_pdsecret_workfunc_locked(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, check_pd_secret_work.work);

	if (!info->verify_process_end) {
		mca_vote(info->input_limit_voter, "icl_limit", false,
			 STATEGY_INPUT_DEFAULT_VALUE);
		info->verify_process_end = 1;
		mca_log_info(
			"cancel icl_limit vote and pd_end flag when using pd charger\n");
	}
}

#define NTC_SCALE_BOARDTEMP 100
static int strategy_buckchg_thermal_notifier_cb(struct notifier_block *nb,
						unsigned long event, void *val)
{
	struct strategy_buckchg_dev *info =
		container_of(nb, struct strategy_buckchg_dev, thermal_board_nb);

	if (!val || READ_ONCE(info->stopping))
		return NOTIFY_DONE;
	switch (event) {
	case MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE:
		WRITE_ONCE(info->thermal_board_temp,
			   *(int *)val / NTC_SCALE_BOARDTEMP);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static int strategy_buckchg_panel_notifier_cb(struct notifier_block *nb,
					      unsigned long event, void *data)
{
	struct strategy_buckchg_dev *info =
		container_of(nb, struct strategy_buckchg_dev, panel_nb);
	int screen_state;

	if (!data || READ_ONCE(info->stopping) ||
	    event != MCA_EVENT_PANEL_SCREEN_STATE_CHANGE)
		return NOTIFY_DONE;

	screen_state = *(int *)data;
	mca_log_info("panel screen state: %d, last: %d\n", screen_state,
		     info->screen_on);
	WRITE_ONCE(info->screen_on, screen_state != 0);
	return NOTIFY_DONE;
}

static void
strategy_rerun_handle_pd_auth_workfunc_locked(struct work_struct *work)
{
	mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
			       MCA_EVENT_USB_STS_CHANGE, NULL);
	mca_log_info("rerun handle pd auth\n");
}

static int strategy_buckchg_check_user(struct strategy_buckchg_dev *info,
				       const char *user)
{
	if (!info || !user || !*user || strnlen(user, 64) == 64)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	return 0;
}

static int strategy_buckchg_if_set_chg_cur(const char *user, char *value,
					   void *data)
{
	struct strategy_buckchg_dev *info = data;
	int current_value;
	int ret = strategy_buckchg_check_user(info, user);

	if (ret)
		return ret;
	if (!value || kstrtoint(value, 0, &current_value) ||
	    current_value < 0 || current_value > 30000)
		return -EINVAL;
	return mca_vote(info->charge_limit_voter, user, current_value != 0,
			current_value);
}

static int strategy_buckchg_if_get_chg_cur(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	client_str = mca_get_effective_client(info->charge_limit_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->charge_limit_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		  client_str, value);

	return 0;
}

#define MTBF_CLIENT_SUBSTR_UPPER "MTBF\0"
#define MTBF_CLIENT_SUBSTR_LOW "mtbf\0"
#define MTBF_ACTIVE_TEST_CLIENT_SUBSTR "shell\0"
#define MTBF_TEST_MAX_CURRENT_MA 1500
static int strategy_buckchg_if_set_input_cur(const char *user, char *value,
					     void *data)
{
	struct strategy_buckchg_dev *info = data;
	int current_value;
	bool override;
	int ret = strategy_buckchg_check_user(info, user);

	if (ret)
		return ret;
	if (!value || kstrtoint(value, 0, &current_value) ||
	    current_value < 0 || current_value > 30000)
		return -EINVAL;
	override = strstr(user, MTBF_CLIENT_SUBSTR_UPPER) ||
		   strstr(user, MTBF_CLIENT_SUBSTR_LOW) ||
		   strstr(user, MTBF_ACTIVE_TEST_CLIENT_SUBSTR);
	if (strstr(user, MTBF_ACTIVE_TEST_CLIENT_SUBSTR))
		current_value = min(current_value, MTBF_TEST_MAX_CURRENT_MA);
	/* Releasing a shell vote must release the same override it enabled. */
	if (override)
		return mca_vote_override(info->input_limit_voter, user,
					 current_value != 0, current_value);
	return mca_vote(info->input_limit_voter, user, current_value != 0,
			current_value);
}

static int strategy_buckchg_if_get_input_cur(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	client_str = mca_get_effective_client(info->input_limit_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->input_limit_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		  client_str, value);

	return 0;
}

static int strategy_buckchg_if_set_chg_en(const char *user, unsigned int value,
					  void *data)
{
	struct strategy_buckchg_dev *info = data;
	int ret = strategy_buckchg_check_user(info, user);

	if (ret)
		return ret;
	if (value > 1)
		return -EINVAL;
	if (value)
		strategy_buckchg_clear_pmic_temp_term(info);
	return mca_vote(info->chg_enable_voter, user, true, value);
}

static int strategy_buckchg_if_get_chg_en(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	client_str = mca_get_effective_client(info->chg_enable_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->chg_enable_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		  client_str, value);

	return 0;
}

static int strategy_buckchg_if_get_input_suspend(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	client_str = mca_get_effective_client(info->input_suppend_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->input_suppend_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		  client_str, value);

	return 0;
}

static int strategy_buckchg_if_set_input_suspend(const char *user, char *value,
						 void *data)
{
	struct strategy_buckchg_dev *info = data;
	int suspend;
	int ret = strategy_buckchg_check_user(info, user);

	if (ret)
		return ret;
	if (!value || kstrtoint(value, 0, &suspend) || suspend < 0 ||
	    suspend > 1)
		return -EINVAL;
	ret = mca_vote(info->input_suppend_voter, user, true, suspend);
	if (!ret)
		strategy_buckchg_clear_pmic_temp_term(info);
	return ret;
}

static int strategy_buckchg_if_set_ship_mode(const char *user, unsigned int val,
					     void *data)
{
	struct strategy_buckchg_dev *info = data;
	int ret = strategy_buckchg_check_user(info, user);

	if (ret)
		return ret;
	if (val > 1)
		return -EINVAL;
	return platform_class_buckchg_ops_set_ship_mode(MAIN_BUCK_CHARGER, val);
}

static int strategy_buckchg_if_get_ship_mode(bool *val, void *data)
{
	struct strategy_buckchg_dev *info = data;
	int rc;

	if (!info || !val)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;

	rc = platform_class_buckchg_ops_get_ship_mode(0, val);
	if (rc < 0)
		return rc;

	return 0;
}

static const struct mca_charge_if_ops g_strategy_buckchg_if_ops = {
	.type_name = "buck",
	.set_input_suspend = strategy_buckchg_if_set_input_suspend,
	.get_input_suspend = strategy_buckchg_if_get_input_suspend,
	.set_charge_enable = strategy_buckchg_if_set_chg_en,
	.get_charge_enable = strategy_buckchg_if_get_chg_en,
	.set_input_current_limit = strategy_buckchg_if_set_input_cur,
	.get_input_current_limit = strategy_buckchg_if_get_input_cur,
	.set_charge_current_limit = strategy_buckchg_if_set_chg_cur,
	.get_charge_current_limit = strategy_buckchg_if_get_chg_cur,
	.set_ship_mode_en = strategy_buckchg_if_set_ship_mode,
	.get_ship_mode_status = strategy_buckchg_if_get_ship_mode,
};

static int strategy_buckchg_soc_limit_sts_callback(void *data,
						   int effective_result)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || effective_result < 0 || effective_result > 1)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->soc_limit_more_enable, effective_result);
	/* Smart-charge holds its state mutex during this callback. */
	mod_delayed_work(info->wq, &info->soc_limit_stepper_work, 0);
	return 0;
}

static int mca_charger_buckchg_pwr_boost_sts_callback(void *data, int enable)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || enable < 0 || enable > 1)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	WRITE_ONCE(info->smartchg_data.pwr_boost_state, enable);
	return 0;
}

static int strategy_buckchg_smartchg_set_fcc_callback(void *data, int val)
{
	struct strategy_buckchg_dev *info = data;

	if (!info || val < 0 || val > 30000)
		return -EINVAL;
	if (READ_ONCE(info->stopping))
		return -ENODEV;
	return mca_vote(info->charge_limit_voter, "smartchg", val != 0, val);
}

static const struct mca_smartchg_if_ops g_buck_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_BUCK,
	.data = NULL,
	.set_soc_limit_sts = strategy_buckchg_soc_limit_sts_callback,
	.set_pwr_boost_sts = mca_charger_buckchg_pwr_boost_sts_callback,
	.set_fcc = strategy_buckchg_smartchg_set_fcc_callback,
};

static void strategy_buckchg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, monitor_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->monitor_active)
		return;
	strategy_buckchg_monitor_workfunc_locked(work);
}

static void strategy_buckchg_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, sw_cv_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->sw_cv_running ||
	    !info->monitor_active)
		return;
	strategy_buckchg_sw_cv_workfunc_locked(work);
}

static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, base_flip_sw_cv_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->sw_cv_running ||
	    !info->monitor_active)
		return;
	strategy_buckchg_base_flip_sw_cv_workfunc_locked(work);
}

static void strategy_wls_revchg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info =
		container_of(work, struct strategy_buckchg_dev,
			     wls_revchg_monitor_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->wls_revchg_en)
		return;
	strategy_wls_revchg_monitor_workfunc_locked(work);
}

static void strategy_csd_pulse_process_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, csd_pulse_process_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->proc_data.online)
		return;
	strategy_csd_pulse_process_workfunc_locked(work);
}

static void strategy_source_status_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info =
		container_of(work, struct strategy_buckchg_dev,
			     source_status_monitor_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->last_otg_present)
		return;
	strategy_source_status_monitor_workfunc_locked(work);
}

static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, check_pd_secret_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping) || !info->proc_data.online)
		return;
	strategy_buckchg_check_pdsecret_workfunc_locked(work);
}

static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info =
		container_of(work, struct strategy_buckchg_dev,
			     rerun_handle_pd_auth_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping))
		return;
	strategy_rerun_handle_pd_auth_workfunc_locked(work);
}

static void
strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(
		work, struct strategy_buckchg_dev, soc_limit_stepper_work.work);

	guard(mutex)(&info->policy_lock);
	if (READ_ONCE(info->stopping))
		return;
	strategy_buckchg_soc_limit_stepper_workfunc_locked(work);
}

static int strategy_buckchg_process_event(int event, int value, void *data)
{
	struct strategy_buckchg_dev *info = data;
	unsigned long flags;
	unsigned int tail;
	int ret = 0;

	if (!info)
		return -EINVAL;
	if (event == MCA_EVENT_WIRELESS_REVCHG &&
	    !IS_ENABLED(CONFIG_MCA_BUCK_WLS_REV_COORDINATION))
		return -EOPNOTSUPP;
	spin_lock_irqsave(&info->event_lock, flags);
	if (READ_ONCE(info->stopping)) {
		ret = -ENODEV;
	} else if (info->event_count == BUCK_EVENT_QUEUE_SIZE) {
		ret = -ENOSPC;
	} else {
		tail = (info->event_head + info->event_count) %
		       BUCK_EVENT_QUEUE_SIZE;
		info->events[tail] =
			(struct strategy_buckchg_event){ event, value };
		info->event_count++;
		queue_work(info->wq, &info->event_work);
	}
	spin_unlock_irqrestore(&info->event_lock, flags);
	return ret;
}

static void strategy_buckchg_event_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info =
		container_of(work, struct strategy_buckchg_dev, event_work);
	struct strategy_buckchg_event event;
	unsigned long flags;

	for (;;) {
		spin_lock_irqsave(&info->event_lock, flags);
		if (!info->event_count || READ_ONCE(info->stopping)) {
			spin_unlock_irqrestore(&info->event_lock, flags);
			return;
		}
		event = info->events[info->event_head];
		info->event_head =
			(info->event_head + 1) % BUCK_EVENT_QUEUE_SIZE;
		info->event_count--;
		spin_unlock_irqrestore(&info->event_lock, flags);
		mutex_lock(&info->policy_lock);
		if (!READ_ONCE(info->stopping))
			strategy_buckchg_handle_event(event.event, event.value,
						      info);
		mutex_unlock(&info->policy_lock);
	}
}

static void strategy_buckchg_stop(struct strategy_buckchg_dev *info)
{
	unsigned long flags;

	if (!info || !info->wq)
		return;
	spin_lock_irqsave(&info->event_lock, flags);
	WRITE_ONCE(info->stopping, true);
	info->event_count = 0;
	spin_unlock_irqrestore(&info->event_lock, flags);
	if (info->panel_registered)
		mca_event_block_notify_unregister(MCA_EVENT_TYPE_PANEL,
						  &info->panel_nb);
	if (info->thermal_registered)
		mca_event_block_notify_unregister(MCA_EVENT_TYPE_THERMAL_TEMP,
						  &info->thermal_board_nb);
	if (info->smart_registered)
		mca_smartchg_if_ops_unregister(MCA_SMARTCHG_IF_CHG_TYPE_BUCK,
					       info);
	if (info->interface_registered)
		mca_charge_if_ops_unregister("buck", info);
	if (info->strategy_registered)
		mca_strategy_ops_unregister(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					    info);
	disable_delayed_work_sync(&info->monitor_work);
	disable_delayed_work_sync(&info->sw_cv_work);
	disable_delayed_work_sync(&info->base_flip_sw_cv_work);
	disable_delayed_work_sync(&info->wls_revchg_monitor_work);
	disable_delayed_work_sync(&info->csd_pulse_process_work);
	disable_delayed_work_sync(&info->source_status_monitor_work);
	disable_delayed_work_sync(&info->check_pd_secret_work);
	disable_delayed_work_sync(&info->rerun_handle_pd_auth_work);
	disable_delayed_work_sync(&info->soc_limit_stepper_work);
	cancel_work_sync(&info->event_work);
	destroy_workqueue(info->wq);
	info->wq = NULL;
	/* Hardware is quiesced after every work and named-voter reader drains. */
	strategy_buckchg_destroy_voters(info);
	strategy_class_buckchg_ops_set_chg(info, false);
	strategy_class_buckchg_ops_adc_enable(info, false);
	info->strategy_init_done = 0;
}

static int strategy_buckchg_validate_config(struct strategy_buckchg_dev *info)
{
	const unsigned int currents[] = {
		info->in_dcp,
		info->in_pd,
		info->in_pps,
		info->in_hvdcp,
		info->in_hvdcp3,
		info->in_hvdcp3p5,
		info->in_cdp,
		info->in_sdp,
		info->in_float,
		info->chg_dcp,
		info->chg_pd,
		info->chg_pps,
		info->chg_hvdcp,
		info->chg_hvdcp3,
		info->chg_hvdcp3p5,
		info->chg_cdp,
		info->chg_sdp,
		info->chg_float,
		info->chg_batt_auth_failed,
	};
	const int compensation[] = {
		info->pmic_fv_compensation,
		info->pmic_fv_compensation_cold,
		info->pmic_fv_compensation_hot,
		info->pmic_middle_fv_compensation,
		info->pmic_high_fv_compensation,
		info->pmic_wls_fv_compensation,
		info->pmic_iterm_compensation,
		info->curr_terminate_compensation,
	};
	unsigned int i;

	if (info->support_multi_buck)
		return -EOPNOTSUPP;
	for (i = 0; i < ARRAY_SIZE(currents); i++)
		if (currents[i] > 30000)
			return -ERANGE;
	for (i = 0; i < ARRAY_SIZE(compensation); i++)
		if (compensation[i] < 0 || compensation[i] > 1000)
			return -ERANGE;
	if (info->sw_cv_fcc_step <= 0 || info->sw_cv_fcc_step > 1000 ||
	    info->sw_cv_fv_step < 0 || info->sw_cv_fv_step > 100 ||
	    info->sw_cv_fcc_limit_buffer < 0 ||
	    info->sw_cv_fcc_limit_buffer > 30000 ||
	    info->ffc_temp_low >= info->ffc_temp_high ||
	    info->ffc_temp_high > 80 ||
	    info->allow_start_ffc_batt_soc_thr < 0 ||
	    info->allow_start_ffc_batt_soc_thr > 100)
		return -ERANGE;
	for (i = 0; i < REV_USBIN_TYPE_MAX; i++)
		if (info->rev_vadp_valid_l[i] < 5000 ||
		    info->rev_vadp_valid_h[i] > 20000 ||
		    info->rev_vadp_valid_l[i] > info->rev_req_vadp[i] ||
		    info->rev_req_vadp[i] > info->rev_vadp_valid_h[i])
			return -ERANGE;
	return 0;
}

static int strategy_buckchg_class_probe(struct platform_device *pdev)
{
	struct strategy_buckchg_dev *info;
	struct mca_charge_if_ops charge_ops = g_strategy_buckchg_if_ops;
	struct mca_smartchg_if_ops smart_ops = g_buck_smartchg_if_ops;
	int online = 0;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	mutex_init(&info->policy_lock);
	spin_lock_init(&info->event_lock);
	info->last_pos = 1;
	info->proc_data.voltage = STATEGY_CHARGE_VBUS_5V;
	info->buck_5v_in = STATEGY_INPUT_5V_DEFAULT_VALUE;
	info->buck_9v_in = STATEGY_INPUT_9V_DEFAULT_VALUE;
	info->buck_5v_ich = STATEGY_CHARGE_CURRENT_DEFAULT_VALUE;
	info->buck_9v_ich = STATEGY_CHARGE_CURRENT_DEFAULT_VALUE;
	strategy_buckchg_parse_dt(info);
	ret = strategy_buckchg_validate_config(info);
	if (ret)
		return ret;
	/* The class uses -1 when no hardware provider has registered yet. */
	ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	if (ret)
		return ret == -1 || ret == -ENODEV ? -EPROBE_DEFER : ret;
	INIT_WORK(&info->event_work, strategy_buckchg_event_workfunc);
	INIT_DELAYED_WORK(&info->monitor_work,
			  strategy_buckchg_monitor_workfunc);
	INIT_DELAYED_WORK(&info->sw_cv_work, strategy_buckchg_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->base_flip_sw_cv_work,
			  strategy_buckchg_base_flip_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->wls_revchg_monitor_work,
			  strategy_wls_revchg_monitor_workfunc);
	INIT_DELAYED_WORK(&info->csd_pulse_process_work,
			  strategy_csd_pulse_process_workfunc);
	INIT_DELAYED_WORK(&info->source_status_monitor_work,
			  strategy_source_status_monitor_workfunc);
	INIT_DELAYED_WORK(&info->check_pd_secret_work,
			  strategy_buckchg_check_pdsecret_workfunc);
	INIT_DELAYED_WORK(&info->rerun_handle_pd_auth_work,
			  strategy_rerun_handle_pd_auth_workfunc);
	INIT_DELAYED_WORK(&info->soc_limit_stepper_work,
			  strategy_buckchg_soc_limit_stepper_workfunc);
	info->wq = alloc_ordered_workqueue("mca_buck_policy", WQ_MEM_RECLAIM);
	if (!info->wq)
		return -ENOMEM;
	ret = strategy_buckchg_init_voter(info);
	if (ret)
		goto fail;
	ret = mca_vote(info->input_voltage_voter, "batt_auth", true, 0);
	if (ret)
		goto fail;
	ret = mca_vote(info->charge_limit_voter, "batt_auth", true,
		       info->chg_batt_auth_failed);
	if (ret)
		goto fail;
	info->strategy_init_done = 1;
	platform_set_drvdata(pdev, info);
	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					strategy_buckchg_process_event,
					strategy_buckchg_get_status,
					strategy_buckchg_set_config, info);
	if (ret)
		goto fail;
	info->strategy_registered = true;
	charge_ops.data = info;
	ret = mca_charge_if_ops_register(&charge_ops);
	if (ret)
		goto fail;
	info->interface_registered = true;
	smart_ops.data = info;
	ret = mca_smartchg_if_ops_register(&smart_ops);
	if (ret)
		goto fail;
	info->smart_registered = true;
	info->thermal_board_nb.notifier_call =
		strategy_buckchg_thermal_notifier_cb;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP,
					      &info->thermal_board_nb);
	if (ret)
		goto fail;
	info->thermal_registered = true;
	info->panel_nb.notifier_call = strategy_buckchg_panel_notifier_cb;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL,
					      &info->panel_nb);
	if (ret)
		goto fail;
	info->panel_registered = true;
	ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	if (ret)
		goto fail;
	if (online)
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
				       MCA_EVENT_USB_CONNECT, NULL);
	return 0;
fail:
	strategy_buckchg_stop(info);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static void strategy_buckchg_class_remove(struct platform_device *pdev)
{
	strategy_buckchg_stop(platform_get_drvdata(pdev));
	platform_set_drvdata(pdev, NULL);
}

static void strategy_buckchg_class_shutdown(struct platform_device *pdev)
{
	strategy_buckchg_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,strategy_buckchg" },
	{},
};

MODULE_DEVICE_TABLE(of, match_table);

static struct platform_driver strategy_buckchg_class_driver = {
	.driver	= {
		.name = "strategy_buckchg_class",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = strategy_buckchg_class_probe,
	.remove = strategy_buckchg_class_remove,
	.shutdown = strategy_buckchg_class_shutdown,
};

static int __init strategy_buckchg_class_init(void)
{
	return platform_driver_register(&strategy_buckchg_class_driver);
}
module_init(strategy_buckchg_class_init);

static void __exit strategy_buckchg_class_exit(void)
{
	platform_driver_unregister(&strategy_buckchg_class_driver);
}
module_exit(strategy_buckchg_class_exit);

MODULE_DESCRIPTION("strategy buckchg class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
