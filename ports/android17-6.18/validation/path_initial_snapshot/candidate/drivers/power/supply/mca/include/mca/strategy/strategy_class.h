#ifndef _MCA_STRATEGY_STRATEGY_CLASS_H_
#define _MCA_STRATEGY_STRATEGY_CLASS_H_

#include <linux/types.h>

enum mca_strategy_func_type {
	STRATEGY_FUNC_TYPE_BUCK_CHARGE = 0,
	STRATEGY_FUNC_TYPE_QUICK_CHARGE,
	STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
	STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
	STRATEGY_FUNC_TYPE_REV_WIRELESS,
	STRATEGY_FUNC_TYPE_THERMAL,
	STRATEGY_FUNC_TYPE_FG,
	STRATEGY_FUNC_TYPE_JEITA,
	STRATEGY_FUNC_TYPE_SMARTCHG,
	STRATEGY_FUNC_TYPE_BMD,
	STRATEGY_FUNC_TYPE_CONNECTOR_ANTIBURN,
	STRATEGY_FUNC_TYPE_MAX,
};

enum strategy_status_type {
	STRATEGY_STATUS_TYPE_ONLINE = 0,
	STRATEGY_STATUS_TYPE_CHARGING,
	STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
	STRATEGY_STATUS_TYPE_QC_ENABLE,
	STRATEGY_STATUS_TYPE_QC_TYPE,
	STRATEGY_STATUS_TYPE_QC_IBAT_MAX,
	STRATEGY_STATUS_TYPE_QC_START_FLAG,
	STRATEGY_STATUS_TYPE_REV_TEST,
	STRATEGY_STATUS_TYPE_WLS_MAGNET_LIMIT,
	STRATEGY_STATUS_TYPE_POWER_MAX,
	STRATEGY_STATUS_TYPE_ENABLE,
	STRATEGY_STATUS_TYPE_MODE,
	STRATEGY_STATUS_TYPE_VBUS,
	STRATEGY_STATUS_TYPE_IBUS,
	STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM,
	STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM,
	STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM,
	STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE,
	STRATEGY_STATUS_TYPE_QC_TAPER_DONE_NO_RETRY,
	STRATEGY_STATUS_TYPE_FG_BATT_MATCH,
	STRATEGY_STATUS_TYPE_BMD_BATT_MISSING,
	STRATEGY_STATUS_TYPE_QC_MAX_POWER,
	STRATEGY_STATUS_TYPE_FG_FIRST_TERM,
	STRATEGY_STATUS_TYPE_QC_TERM_CURR,
	STRATEGY_STATUS_TYPE_QC_TERM_VOLT,
};

enum adp_icon_type {
	ADP_ICON_TYPE_NORMAL = 0,
	ADP_ICON_TYPE_FAST,
	ADP_ICON_TYPE_FLASH,
	ADP_ICON_TYPE_TURBO,
	ADP_ICON_TYPE_SUPER,
};

typedef enum {
	REV_EN_BOOST = 0,
	OTG_EN_BOOST,
	WIRELESS_EN_BOOST,
} EN_SRC;

enum mca_wls_boost_src {
	PMIC_REV_BOOST = 0,
	PMIC_HBOOST,
	EXTERNAL_BOOST,
	CHARGER_ADAPTER,
	BOOST_SRC_MAX,
};

enum mca_strategy_config_type {
	STRATEGY_CONFIG_INPUT_CURRENT_LIMIT = 0,
	STRATEGY_CONFIG_MAX,
};

enum mca_buck_chg_status {
	MCA_BUCK_CHG_STS_NA = 0,
	MCA_BUCK_CHG_NO_CHARGING,
	MCA_BUCK_CHG_STS_CHARGING,
	MCA_BUCK_CHG_STS_CHARGE_DONE,
};

enum mca_quick_chg_status {
	MCA_QUICK_CHG_STS_NO_CHARGING = 0,
	MCA_QUICK_CHG_STS_CHARGING,
	MCA_QUICK_CHG_STS_CHARGE_DONE,
	MCA_QUICK_CHG_STS_CHARGE_FAILED,
};

typedef int (*mca_strategy_func)(int event, int value, void *data);
typedef int (*mca_strategy_get_status)(int status, void *value, void *data);
typedef int (*mca_strategy_set_config)(int config, int value, void *data);

int mca_get_wls_charger_thermal_remove(bool *wls_thermal_remove);
int mca_set_wls_charger_thermal_remove(bool wls_thermal_remove);

int mca_strategy_func_get_status(int type, int status, void *value);
int mca_strategy_func_process(unsigned int type, int event, int value);
int mca_strategy_func_set_config(int type, int config, int value);
int mca_strategy_ops_register(unsigned int type, mca_strategy_func func,
			      mca_strategy_get_status get_func,
			      mca_strategy_set_config set_config, void *data);

/* Unregister before freeing callback data; not callable from a callback. */
void mca_strategy_ops_unregister(unsigned int type, void *data);

#endif /* _MCA_STRATEGY_STRATEGY_CLASS_H_ */
