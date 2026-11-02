#ifndef _MCA_HARDWARE_HW_PATH_CONTROL_H_
#define _MCA_HARDWARE_HW_PATH_CONTROL_H_

#include <linux/types.h>

typedef enum {
	PATH_CONTROL_USB = 0,
	PATH_CONTROL_WLS,
	PATH_CONTROL_WLS_REV,
	PATH_CONTROL_OTG,
	PATH_CONTROL_VDD,
} CONTROL_SRC;

enum mca_otg_enable_sts {
	OTG_DISABLE = 0,
	OTG_ENABLE,
	OTG_ENABLE_SEQUENCE,
};

int mca_path_control_enable_gate(CONTROL_SRC src, bool enable);

#endif /* _MCA_HARDWARE_HW_PATH_CONTROL_H_ */
