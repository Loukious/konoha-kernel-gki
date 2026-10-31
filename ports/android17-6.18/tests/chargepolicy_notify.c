#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define READ_ONCE(x) (x)
#define mca_log_info(...) ((void)0)
#define mca_log_err(...) ((void)0)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
/* ENUM_DEFINITIONS */
/* EVENT_DEFINITIONS */
static int events, writes, fault;
static void *system_wq;
struct qcom_subpmic { void *dev; bool stopping; int update_usb_type_work; int lpd_status,cc_short_vbus,pps_ptf,eu_model,cp_vbus_revert,sbu_lpd;u8 sink_pwr_suspend; };
#define pm_wakeup_dev_event(...) (events++)
#define queue_delayed_work(...) (events++)
#define mca_event_block_notify(...) (events++)
#define mca_strategy_func_process(...) (events++)
#define platform_class_cp_set_revchg(...) (events++)
#define qcom_subpmic_report_uevent(...) (events++)
#define qcom_subpmic_queue_notify(...) (events++)
static int mca_adsp_glink_write_prop(int prop, const void *data, size_t len) { (void)prop;assert(data&&len>0&&len<=4);writes++;return fault; }
/* STAGED_DEFINITIONS */
int main(void) {
 struct qcom_subpmic sc={0};
 for(int type=1;type<=11;type++)for(int len=0;len<4;len++) {void *p=malloc(len?len:1);memset(p,0,len?len:1);events=0;qcom_subpmic_notify_cb(type,p,len,&sc);assert(events==0);free(p);}
 u8 unaligned[5]={0,1,0,0,0};events=0;qcom_subpmic_notify_cb(SUBPMIC_NOTIFY_LPD_STATUS,unaligned+1,4,&sc);assert(sc.lpd_status==1&&events>0);
 events=0;sc.stopping=true;qcom_subpmic_notify_cb(SUBPMIC_NOTIFY_LPD_STATUS,unaligned+1,4,&sc);assert(!events);sc.stopping=false;
 events=0;qcom_subpmic_notify_cb(SUBPMIC_NOTIFY_USB_TYPE,NULL,0,&sc);assert(events>0);
 fault=-EIO;assert(qcom_subpmic_enable_charging(NULL,true)==-EIO);assert(qcom_subpmic_set_term_volt(NULL,4450)==-EIO);assert(qcom_subpmic_set_input_current_limit(NULL,1500)==-EIO);assert(writes==3);
 fault=0;assert(!qcom_subpmic_enable_charging(NULL,false));
 puts("{\"passed\":true,\"short_notifications\":44,\"unaligned_notifications\":true,\"stopped_callbacks\":true,\"charger_programming_errors\":true}");
}
