#ifndef _MCA_PLATFORM_PLATFORM_CP_CLASS_H_
#define _MCA_PLATFORM_PLATFORM_CP_CLASS_H_
#include <linux/types.h>
enum mca_cp_role {
	CP_ROLE_MASTER = 0,
	CP_ROLE_SLAVE,
	CP_ROLE_THIRD,
	CP_ROLE_MAX,
};

enum mca_cp_charge_mode {
	CP_MODE_FORWARD_4_1 = 0,
	CP_MODE_FORWARD_2_1,
	CP_MODE_FORWARD_1_1,
	CP_MODE_FORWARD_1_1_REVERSED,
	CP_MODE_REVERSE_1_4,
	CP_MODE_REVERSE_1_2,
	CP_MODE_REVERSE_1_1,
	CP_MODE_REVERSE_1_1_REVERSED,
	CP_MODE_MAX,
};
enum mca_cp_status_type {
	VBUS_ERRORHI_STAT = 0,
	VBUS_ERRORLO_STAT,
	VBUS_PRESENT_STAT,
	CP_VBUS_PRESENT_STAT,
	VOUT_INSERT_STAT,
	VOUT_OK_CHG_STAT,
	VOUT_OK_REV_STAT,
	VOUT_OVP_STAT,
	VOUT_TH_SW_REGN_STAT,
	VUSB_PRESENT_STAT,
	VWPC_PRESENT_STAT,
	VAC1_INSERT_STAT,
	VAC2_INSERT_STAT,
	VAC1_OVP_STAT,
	VAC2_OVP_STAT,
	VBAT_INSERT_STAT,
	VBAT_OVP_STAT,
	VBAT_OVP_ALM_STAT,
	VBUS_OVP_STAT,
	VBUS_TH_CHG_EN_STAT,
	IBUS_OCP_STAT,
	IBUS_UCP_FALL_STAT,
	ACDRV1_STAT,
	ACDRV2_STAT,
	ACRB1_CFG_STAT,
	ACRB2_CFG_STAT,
	ADC_DONE_STAT,
	CP_SWITCHING_STAT,
	PIN_DIAG_FAIL_STAT,
	SS_TIMEOUT_STAT,
	TDIE_ALM_STAT,
	TSBAT_FLT_STAT,
	TSHUT_FLT_STAT,
	TSHUT_STAT,
	WD_TIMEOUT_STAT,
};

enum mca_cp_pmid_error {
	CP_PMID_ERROR_OK = 0,
	CP_PMID_ERROR_LOW,
	CP_PMID_ERROR_HIGH,
};

struct platform_class_cp_ops {
	int (*cp_set_enable)(bool, void *data);
	int (*cp_get_enabled)(bool *, void *data);
	int (*cp_set_present)(bool, void *data);
	int (*cp_get_vbus_present)(bool *, void *data);
	int (*cp_get_present)(bool *, void *data);
	int (*cp_get_battery_present)(bool *, void *data);
	int (*cp_get_battery_voltage)(u32 *, void *data);
	int (*cp_get_battery_current)(u32 *, void *data);
	int (*cp_get_battery_temperature)(u32 *, void *data);
	int (*cp_get_bus_voltage)(u32 *, void *data);
	int (*cp_get_bus_current)(u32 *, void *data);
	int (*cp_get_bus_temperature)(int *, void *data);
	int (*cp_get_die_temperature)(int *, void *data);
	int (*cp_get_alarm_status)(int *, void *data);
	int (*cp_get_fault_status)(int *, void *data);
	int (*cp_get_bus_error_status)(int *, void *data);
	int (*cp_get_reg_status)(int *, void *data);
	int (*cp_set_mode)(int, void *data);
	int (*cp_get_mode)(int *, void *data);
	int (*cp_device_init)(int, void *data);
	int (*cp_enable_adc)(bool, void *data);
	int (*cp_get_bypass_support)(bool *, void *data);
	int (*cp_dump_register)(void *data);
	int (*cp_get_chip_vendor)(int *, void *data);
	int (*cp_get_probe_ok)(void *data);
	int (*cp_get_int_stat)(int, bool *, void *data);
	int (*cp_enable_acdrv_manual)(bool, void *data);
	int (*cp_enable_wpcgate)(bool, void *data);
	int (*cp_enable_ovpgate)(bool, void *data);
	int (*cp_enable_ovpgate_with_check)(int, bool, void *data);
	int (*cp_get_ovpgate_status)(bool *, void *data);
	int (*cp_get_usb_voltage)(u32 *, void *data);
	int (*cp_get_errorhl_stat)(int *, void *data);
	int (*cp_enable_busucp)(bool, void *data);
	int (*cp_set_fsw)(int, void *data);
	int (*cp_set_default_fsw)(void *data);
	int (*cp_get_fsw)(int *, void *data);
	int (*cp_get_fsw_step)(int *, void *data);
	int (*cp_get_tdie)(int *, void *data);
	int (*cp_set_qb)(bool, void *data);
	int (*cp_set_rcp)(bool, void *data);
	int (*cp_set_pmid2outuvp_th)(int, void *data);
	int (*cp_set_revchg)(bool, void *data);
	int (*cp_set_adjustadble_timeout)(int, void *data);
	int (*cp_get_battery_vout)(u32 *, void *data);
	int (*cp_get_adc_enabled)(bool *, void *data);
	int (*cp_set_busovp)(int, void *data);
	int (*cp_enable_vbus_errorhi)(bool, void *data);
	int (*cp_enable_vbus_errorlo)(bool, void *data);
	int (*cp_set_manual_revchg_mode)(bool, void *data);
	int (*cp_set_cp_reverse_mode)(bool, void *data);
};
int platform_class_cp_register_ops(unsigned int role,
				   struct platform_class_cp_ops *ops,
				   void *data);
int platform_class_cp_set_charging_enable(unsigned int role, bool en);
int platform_class_cp_get_charging_enabled(unsigned int role, bool *en);
int platform_class_cp_set_present(unsigned int role, bool present);
int platform_class_cp_set_revchg(unsigned int role, bool en);
int platform_class_cp_set_adjustadble_timeout(unsigned int role, int timeout);
int platform_class_cp_get_vbus_present(unsigned int role, bool *present);
int platform_class_cp_get_present(unsigned int role, bool *present);
int platform_class_cp_get_battery_present(unsigned int role, bool *present);
int platform_class_cp_get_battery_voltage(unsigned int role, int *vbat);
int platform_class_cp_get_battery_current(unsigned int role, int *ibat);
int platform_class_cp_get_battery_temperature(unsigned int role, int *tbat);
int platform_class_cp_get_bus_voltage(unsigned int role, int *vbus);
int platform_class_cp_get_bus_current(unsigned int role, int *ibus);
int platform_class_cp_get_bus_temperature(unsigned int role, int *tbus);
int platform_class_cp_get_die_temperature(unsigned int role, int *tdie);
int platform_class_cp_get_alarm_status(unsigned int role, int *alarm_status);
int platform_class_cp_get_fault_status(unsigned int role, int *fault_status);
int platform_class_cp_get_bus_error_status(unsigned int role,
					   int *error_status);
int platform_class_cp_get_reg_status(unsigned int role, int *reg_status);
int platform_class_cp_set_mode(unsigned int role, int mode);
int platform_class_cp_get_mode(unsigned int role, int *mode);
int platform_class_cp_device_init(unsigned int role, int value);
int platform_class_cp_enable_adc(unsigned int role, bool en);
int platform_class_cp_get_bypass_support(unsigned int role, bool *status);
int platform_class_cp_dump_register(unsigned int role);
int platform_class_cp_get_chip_vendor(unsigned int role, int *chip_vendor);
int platform_class_cp_get_probe_ok(unsigned int role);
int platform_class_cp_get_int_stat(unsigned int role, int channel,
				   bool *result);
int platform_class_cp_enable_acdrv_manual(unsigned int role, bool en);
int platform_class_cp_enable_wpcgate(unsigned int role, bool en);
int platform_class_cp_enable_ovpgate(unsigned int role, bool en);
int platform_class_cp_enable_ovpgate_with_check(unsigned int role,
						int type_temp, bool en);
int platform_class_cp_get_ovpgate_status(unsigned int role, bool *en);
int platform_class_cp_get_usb_voltage(unsigned int role, int *vusb);
int platform_class_cp_get_ibus_delta(int *val);
int platform_class_cp_get_ibus_total(int *val);
int platform_class_cp_get_errorhl_stat(unsigned int role, int *stat);
int platform_class_cp_enable_busucp(unsigned int role, bool en);
int platform_class_cp_get_battery_vout(unsigned int role, u32 *val);
int platform_class_cp_get_adc_enabled(unsigned int role, bool *en);
int platform_class_cp_set_busovp(unsigned int role, int val);
int platform_class_cp_enable_vbus_errorhi(unsigned int role, bool en);
int platform_class_cp_enable_vbus_errorlo(unsigned int role, bool en);
int platform_class_cp_set_manual_revchg_mode(unsigned int role, bool en);
int platform_class_cp_set_cp_reverse_mode(unsigned int role, bool en);
int platform_class_cp_set_fsw(unsigned int role, int fsw);
int platform_class_cp_set_default_fsw(unsigned int role);
int platform_class_cp_get_fsw(unsigned int role, int *fsw);
int platform_class_cp_get_fsw_step(unsigned int role, int *fsw_step);
int platform_class_cp_get_tdie(unsigned int role, int *tdie);
int platform_class_cp_set_qb(unsigned int role, bool en);
int platform_class_cp_set_rcp(unsigned int role, bool en);
int platform_class_cp_set_pmid2outuvp_th(unsigned int role, int value);
/* Drain callbacks before freeing provider data; not callable from a callback. */
void platform_class_cp_unregister_ops(unsigned int role, void *data);

#endif /* _MCA_PLATFORM_PLATFORM_CP_CLASS_H_ */
