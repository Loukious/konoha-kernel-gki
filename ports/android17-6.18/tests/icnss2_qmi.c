#include "icnss2_mock.h"
#define WLFW_BDF_DOWNLOAD_REQ_MSG_V01_MAX_MSG_LEN 100
#define WLFW_SERVICE_ID_V01 1
#define WLFW_SERVICE_VERS_V01 2
#define WLFW_SERVICE_WCN_INS_ID_V01 3
#define WLFW_SERVICE_INS_ID_V01 4
#define DMS_QMI_MAX_MSG_LEN 100
#define DMS_SERVICE_ID_V01 5
#define DMS_SERVICE_VERS_V01 1
#define IMSPRIVATE_SERVICE_MAX_MSG_LEN 100
#define IMSPRIVATE_SERVICE_ID_V01 6
#define IMSPRIVATE_SERVICE_VERS_V01 1
#define WCN6750_DEVICE_ID 1
#define WCN7750_DEVICE_ID 2
#define WCN6450_DEVICE_ID 3
#define ICNSS_DEL_SERVER 1
#define ICNSS_WLFW_CONNECTED 2
#define ICNSS_ASSERT(x) assert(x)
struct qmi_handle{bool active;};
struct icnss_priv{bool stopping,qmi_registered,dms_registered,ims_registered;struct qmi_handle qmi,qmi_dms,ims_qmi;unsigned long device_id,state;};
static int wlfw_qmi_ops,qmi_dms_ops,ims_qmi_ops,wlfw_msg_handlers[1],qmi_ims_msg_handlers[1],fail_handle,fail_lookup,initialized,released;
static int qmi_handle_init(struct qmi_handle*q,int size,void*ops,void*handlers){assert(!q->active);if(fail_handle)return -ENOMEM;q->active=true;initialized++;return 0;}
static int qmi_add_lookup(struct qmi_handle*q,int s,int v,int i){assert(q->active);return fail_lookup?-EIO:0;}
static void qmi_handle_release(struct qmi_handle*q){assert(q->active);q->active=false;released++;}
/* FUNCTIONS */
int main(void){struct icnss_priv p={0};int(*inits[])(struct icnss_priv*)={icnss_register_fw_service,icnss_dms_init,icnss_register_ims_service};void(*exits[])(struct icnss_priv*)={icnss_unregister_fw_service,icnss_dms_deinit,icnss_unregister_ims_service};unsigned cases=0;for(int s=0;s<3;s++){fail_handle=1;assert(inits[s](&p)==-ENOMEM);exits[s](&p);assert(initialized==released);fail_handle=0;fail_lookup=1;assert(inits[s](&p)==-EIO);exits[s](&p);assert(initialized==released);fail_lookup=0;for(int i=0;i<100;i++){p.device_id=i%4;assert(!inits[s](&p));exits[s](&p);exits[s](&p);assert(initialized==released);cases++;}p.stopping=true;assert(inits[s](&p)==-ESHUTDOWN);p.stopping=false;}assert(!icnss_register_fw_service(&p));p.stopping=true;assert(!icnss_clear_server(&p));assert(!p.qmi_registered&&initialized==released);p.stopping=false;assert(!icnss_register_fw_service(&p));assert(!icnss_clear_server(&p)&&p.qmi_registered);icnss_unregister_fw_service(&p);assert(initialized==released);printf("{\"qmi_lifecycle_cycles\":%u,\"all_handle_lookup_failures_balanced\":true,\"stop_rejects_registration\":true,\"shutdown_reconnect_does_not_assert\":true}\n",cases);}
