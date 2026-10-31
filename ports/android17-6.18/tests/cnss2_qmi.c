#include "cnss2_mock.h"
#define QMI_WLFW_MAX_RECV_BUF_SIZE 1000
#define CNSS_QMI_WLFW_CONNECTED 0
#define CNSS_QMI_DMS_CONNECTED 1
#define CNSS_DMS_DEL_SERVER 2
#define DMS_QMI_MAX_MSG_LEN 100
#define DMS_SERVICE_ID_V01 1
#define DMS_SERVICE_VERS_V01 1
#define COEX_SERVICE_MAX_MSG_LEN 100
#define COEX_SERVICE_ID_V01 2
#define COEX_SERVICE_VERS_V01 1
#define IMSPRIVATE_SERVICE_MAX_MSG_LEN 100
#define IMSPRIVATE_SERVICE_ID_V01 3
#define IMSPRIVATE_SERVICE_VERS_V01 1
struct qmi_handle { bool active; };
struct qmi_service { int unused; };
struct cnss_plat_data { struct mutex qmi_lock;spinlock_t event_lock;bool stopping,qmi_initialized,dms_initialized,dms_work_initialized,coex_initialized,ims_initialized;unsigned long driver_state;struct qmi_handle qmi_wlfw,qmi_dms,coex_qmi,ims_qmi;struct work_struct dms_del_work; };
static int qmi_wlfw_ops,qmi_dms_ops,coex_qmi_ops,ims_qmi_ops,qmi_wlfw_msg_handlers[1],qmi_ims_msg_handlers[1],fail_handle,fail_lookup,init_count,release_count,lookups;
static void cnss_get_qrtr_info(struct cnss_plat_data *p) { }
static int qmi_handle_init(struct qmi_handle *q,int size,void *ops,void *handlers) { assert(!q->active);if(fail_handle)return fail_handle;q->active=true;init_count++;return 0; }
static int cnss_qmi_add_lookup(struct cnss_plat_data *p) { assert(p->qmi_wlfw.active);lookups++;return fail_lookup; }
static int qmi_add_lookup(struct qmi_handle *q,int service,int version,int instance) { assert(q->active);lookups++;return fail_lookup; }
static void qmi_handle_release(struct qmi_handle *q) { assert(q->active);q->active=false;release_count++; }
static bool schedule_work(struct work_struct *w) { bool old=w->pending;w->pending=1;return !old; }
int cnss_dms_init(struct cnss_plat_data *);
void cnss_dms_deinit(struct cnss_plat_data *);
static void cnss_dms_server_exit_work(struct work_struct *);
/* FUNCTIONS */
int main(void)
{
 struct cnss_plat_data p={0},other={0};
 fail_handle=-ENOMEM;assert(cnss_qmi_init(&p)==-ENOMEM && !p.qmi_initialized);cnss_qmi_deinit(&p);assert(!release_count);
 fail_handle=0;fail_lookup=-ENOSPC;assert(cnss_qmi_init(&p)==-ENOSPC && !p.qmi_initialized);assert(init_count==release_count);
 fail_lookup=0;for(unsigned i=0;i<100;i++) { assert(!cnss_qmi_init(&p) && p.qmi_initialized);int n=init_count;assert(!cnss_qmi_init(&p) && init_count==n);p.driver_state=BIT(CNSS_QMI_WLFW_CONNECTED);cnss_qmi_deinit(&p);assert(!p.qmi_initialized && !p.driver_state);cnss_qmi_deinit(&p);assert(init_count==release_count); }
 p.stopping=true;assert(cnss_qmi_init(&p)==-ESHUTDOWN && !p.qmi_initialized);p.stopping=false;
 int (*init[])(struct cnss_plat_data*)={cnss_dms_init,cnss_register_coex_service,cnss_register_ims_service};
 void (*deinit[])(struct cnss_plat_data*)={cnss_dms_deinit,cnss_unregister_coex_service,cnss_unregister_ims_service};
 for(unsigned service=0;service<3;service++) {
  fail_handle=-ENOMEM;assert(init[service](&p)==-ENOMEM);deinit[service](&p);assert(init_count==release_count);
  fail_handle=0;fail_lookup=-ENOSPC;assert(init[service](&p)==-ENOSPC);deinit[service](&p);assert(init_count==release_count);fail_lookup=0;
  for(unsigned i=0;i<100;i++) { assert(!init[service](&p));deinit[service](&p);deinit[service](&p);assert(init_count==release_count); }
 }
 assert(!cnss_dms_init(&p) && !cnss_dms_init(&other));clear_bit(CNSS_DMS_DEL_SERVER,&p.driver_state);clear_bit(CNSS_DMS_DEL_SERVER,&other.driver_state);
 dms_del_server(&other.qmi_dms,NULL);assert(other.dms_del_work.pending && !p.dms_del_work.pending);
 int n=init_count;cnss_dms_server_exit_work(&other.dms_del_work);assert(other.dms_initialized && init_count==n+1 && p.dms_initialized);
 other.stopping=true;cnss_cancel_dms_work(&other);assert(!other.dms_del_work.pending);dms_del_server(&other.qmi_dms,NULL);assert(!other.dms_del_work.pending);
 n=init_count;cnss_dms_server_exit_work(&other.dms_del_work);assert(!other.dms_initialized && init_count==n);assert(cnss_dms_init(&other)==-ESHUTDOWN);
 cnss_dms_deinit(&p);cnss_dms_deinit(&other);assert(init_count==release_count);
 puts("{\"qmi_rebind_cycles\":400,\"handle_and_lookup_failures_unwind\":true,\"duplicate_release_safe\":true,\"stop_rejects_registration\":true,\"dms_reconnect_is_per_device\":true,\"dms_stop_rejects_requeue\":true,\"failed_optional_service_unregister_safe\":true}");
}
