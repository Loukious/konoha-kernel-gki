#include "ipa_mock.h"
#define IPA_USB_DBG(...) ((void)0)
#define IPA_USB_DBG_LOW(...) ((void)0)
#define IPA_USB_ERR(...) ((void)0)
#define pr_info(...) ((void)0)
#define IS_ENABLED(x) 0
#define IPA_PM_GROUP_DEFAULT 0
enum ipa_usb_notify_event { IPA_USB_DEVICE_READY, IPA_USB_REMOTE_WAKEUP };
enum ipa_usb_teth_prot { IPA_USB_RNDIS, IPA_USB_ECM, IPA_USB_RMNET, IPA_USB_MBIM, IPA_USB_DIAG, IPA_USB_RMNET_CV2X, IPA_USB_MAX_TETH_PROT_SIZE };
enum ipa3_usb_transport_type { IPA_USB_TRANSPORT_TETH, IPA_USB_TRANSPORT_DPL, IPA_USB_TRANSPORT_TETH_2, IPA_USB_TRANSPORT_MAX };
enum ipa3_usb_state { IPA_USB_INVALID, IPA_USB_INITIALIZED, IPA_USB_CONNECTED, IPA_USB_SUSPENDED };
enum ipa3_usb_teth_prot_state { IPA_USB_TETH_PROT_INVALID, IPA_USB_TETH_PROT_INITIALIZED, IPA_USB_TETH_PROT_CONNECTED };
enum ipa3_usb_op { IPA_USB_OP_INIT_TETH_PROT, IPA_USB_OP_DEINIT_TETH_PROT };
enum ipa_client_type { IPA_CLIENT_USB_PROD, IPA_CLIENT_USB_CONS, IPA_CLIENT_USB_DPL_CONS, IPA_CLIENT_USB2_CONS };
#define IPA3_USB_GET_TTYPE(p) ((p)==IPA_USB_DIAG?IPA_USB_TRANSPORT_DPL:((p)==IPA_USB_RMNET_CV2X?IPA_USB_TRANSPORT_TETH_2:IPA_USB_TRANSPORT_TETH))
#define IPA3_USB_IS_TTYPE_DPL(t) ((t)==IPA_USB_TRANSPORT_DPL)
struct ipa_pm_register_params { const char *name; void (*callback)(void*,int); void *user_data; int group; };
struct ipa3_usb_pm_context { struct ipa_pm_register_params reg_params; struct work_struct remote_wakeup_work; u32 hdl; bool associated; };
struct ipa3_usb_transport_type_ctx { struct ipa3_usb_pm_context pm_ctx; int (*ipa_usb_notify_cb)(enum ipa_usb_notify_event,void*); void *user_data; enum ipa3_usb_state state; bool rwakeup_pending; };
struct ipa_usb_teth_params { u8 host_ethaddr[6],device_ethaddr[6]; };
struct ipa_usb_init_params { void (*device_ready_notify)(void); u8 host_ethaddr[6],device_ethaddr[6]; void *private; };
struct ecm_ipa_params { void (*device_ready_notify)(void); u8 host_ethaddr[6],device_ethaddr[6]; void *private; };
struct ipa3_usb_teth_prot_context { enum ipa3_usb_teth_prot_state state; void *user_data; union { struct ipa_usb_init_params rndis; struct ecm_ipa_params ecm; } teth_prot_params; };
struct ipa3_usb_context { struct ipa3_usb_teth_prot_context teth_prot_ctx[IPA_USB_MAX_TETH_PROT_SIZE]; int num_init_prot; struct completion dev_ready_comp; u32 qmi_req_id; spinlock_t state_lock,notify_lock; bool shutting_down,dl_data_pending; struct workqueue_struct *wq; struct mutex general_mutex; struct ipa3_usb_transport_type_ctx ttype_ctx[IPA_USB_TRANSPORT_MAX]; };
static struct ipa3_usb_context *ipa3_usb_ctx;
static int fail_register,fail_associate,fail_deregister,fail_protocol,fail_ready;
static int pm_live,register_calls,deregister_calls,associate_calls,protocol_live,ready_live,queue_drains;
static bool pm_handles[32];
static void ipa3_usb_pm_cb(void *p,int e) {}
static void ipa3_usb_wq_notify_remote_wakeup(struct work_struct *w) {}
static void ipa3_usb_device_ready_notify_cb(void) {}
static int ipa_usb_set_lock_unlock(bool v) { return 0; }
static enum ipa3_usb_state ipa3_usb_get_teth_port_state(void) { return IPA_USB_INITIALIZED; }
static void ipa3_register_client_callback(int (*a)(bool),enum ipa3_usb_state (*b)(void),enum ipa_client_type c) {}
static void ipa3_deregister_client_callback(enum ipa_client_type c) {}
static int ipa_pm_register(struct ipa_pm_register_params *p,u32 *hdl) {
 register_calls++; if(fail_register)return fail_register;
 assert(p->callback && p->user_data && p->name);
 for(unsigned i=1;i<32;i++) {
  if(!pm_handles[i]){pm_handles[i]=true;pm_live++;*hdl=i;return 0;}
 }
 abort();
}
static int ipa_pm_associate_ipa_cons_to_client(u32 hdl,enum ipa_client_type c) { associate_calls++;assert(pm_handles[hdl]);return fail_associate; }
static int ipa_pm_deregister(u32 hdl) { deregister_calls++;assert(hdl<32&&pm_handles[hdl]);if(fail_deregister)return fail_deregister;pm_handles[hdl]=false;pm_live--;return 0; }
static bool ipa3_usb_check_legal_op(enum ipa3_usb_op op,enum ipa3_usb_transport_type t) { return ipa3_usb_ctx->ttype_ctx[t].state!=IPA_USB_CONNECTED; }
static bool ipa3_usb_set_state(enum ipa3_usb_state state,bool err,enum ipa3_usb_transport_type t) { ipa3_usb_ctx->ttype_ctx[t].state=state;return true; }
static int rndis_ipa_init(struct ipa_usb_init_params *p) { if(fail_protocol)return fail_protocol;p->private=kzalloc(1,0);if(!p->private)return -ENOMEM;protocol_live++;return 0; }
static int ecm_ipa_init(struct ecm_ipa_params *p) { if(fail_protocol)return fail_protocol;p->private=kzalloc(1,0);if(!p->private)return -ENOMEM;protocol_live++;return 0; }
static void rndis_ipa_cleanup(void *p) { assert(p&&protocol_live);protocol_live--;kfree(p); }
static void ecm_ipa_cleanup(void *p) { assert(p&&protocol_live);protocol_live--;kfree(p); }
static int ipa3_usb_init_teth_bridge(enum ipa_usb_teth_prot p) { return fail_protocol; }
static void ipa_usb_debugfs_init(void) {}
static void ipa_usb_debugfs_remove(void) {}
static void ipa_ready_callback(void *p) {}
static struct { int unused; } usb_ops;
static int ipa_register_ipa_ready_cb(void (*cb)(void*),void *data) { if(fail_ready)return fail_ready;ready_live++;return 0; }
static void ipa3_unregister_ipa_ready_cb(void (*cb)(void*),void *data) { assert(ready_live==1);ready_live--; }
static int ipa3_usb_register_ready_cb(void);
static void usb_destroy(struct workqueue_struct *w) {
 if(ipa3_usb_ctx->shutting_down){for(int i=0;i<IPA_USB_TRANSPORT_MAX;i++){assert(!ipa3_usb_ctx->ttype_ctx[i].ipa_usb_notify_cb);ipa3_usb_ctx->ttype_ctx[i].pm_ctx.remote_wakeup_work.pending=0;}queue_drains++;}
 destroy_workqueue(w);
}
#define destroy_workqueue(w) usb_destroy(w)
/* FUNCTIONS */
static int notify(enum ipa_usb_notify_event e,void *p) { return 0; }
static int different_notify(enum ipa_usb_notify_event e,void *p) { return 0; }
static void clean(void) { assert(!pm_live&&!protocol_live);ipa3_usb_exit();assert(!ipa3_usb_ctx&&!queues&&!ready_live&&!live); }
int main(void)
{
 struct ipa_usb_teth_params params={{1,2,3,4,5,6},{6,5,4,3,2,1}};unsigned faults=0,cycles=0;
 assert(ipa_usb_init_teth_prot(IPA_USB_DIAG,NULL,notify,(void*)1)==-ENODEV);
 for(int f=1;f<=2;f++){alloc_fault=alloc_step+f;assert(ipa3_usb_init()<0);assert(!ipa3_usb_ctx&&!queues&&!live);faults++;}alloc_fault=0;
 fail_ready=-EIO;assert(ipa3_usb_init()==-EIO&&!ipa3_usb_ctx&&!queues&&!live);fail_ready=0;faults++;
 for(int p=0;p<IPA_USB_MAX_TETH_PROT_SIZE;p++) {
  assert(!ipa3_usb_init());int t=IPA3_USB_GET_TTYPE(p);
  fail_register=-ENOMEM;assert(ipa_usb_init_teth_prot(p,&params,notify,(void*)1)==-ENOMEM);assert(!pm_live&&!ipa3_usb_ctx->ttype_ctx[t].ipa_usb_notify_cb);fail_register=0;faults++;
  fail_associate=-EIO;assert(ipa_usb_init_teth_prot(p,&params,notify,(void*)1)==-EIO);assert(!pm_live&&ipa3_usb_ctx->ttype_ctx[t].pm_ctx.hdl==~0U);fail_associate=0;faults++;
  fail_associate=-EIO;fail_deregister=-EPERM;assert(ipa_usb_init_teth_prot(p,&params,notify,(void*)1)==-EIO);assert(pm_live==1&&!ipa3_usb_ctx->ttype_ctx[t].pm_ctx.associated);fail_associate=fail_deregister=0;faults++;
  int registrations=register_calls;
  assert(!ipa_usb_init_teth_prot(p,&params,notify,(void*)1));assert(register_calls==registrations&&pm_live==1);assert(ipa3_usb_ctx->ttype_ctx[t].pm_ctx.associated);
  assert(ipa_usb_init_teth_prot(p,&params,notify,(void*)1)==-EPERM&&pm_live==1);
  ipa3_usb_ctx->ttype_ctx[t].pm_ctx.remote_wakeup_work.pending=1;
  fail_deregister=-EPERM;assert(ipa_usb_deinit_teth_prot(p)==-EPERM);assert(ipa3_usb_ctx->teth_prot_ctx[p].state==IPA_USB_TETH_PROT_INITIALIZED&&ipa3_usb_ctx->teth_prot_ctx[p].user_data==(void*)1&&pm_live==1);fail_deregister=0;faults++;
  assert(!ipa_usb_deinit_teth_prot(p));assert(!pm_live&&!ipa3_usb_ctx->ttype_ctx[t].ipa_usb_notify_cb&&!ipa3_usb_ctx->ttype_ctx[t].pm_ctx.remote_wakeup_work.pending);clean();cycles++;
 }
 for(int p=0;p<IPA_USB_MAX_TETH_PROT_SIZE;p++)if(p!=IPA_USB_DIAG){assert(!ipa3_usb_init());fail_protocol=-EIO;assert(ipa_usb_init_teth_prot(p,&params,notify,(void*)1)==-EIO);assert(!pm_live&&!protocol_live&&!ipa3_usb_ctx->teth_prot_ctx[p].user_data);fail_protocol=0;clean();faults++;}
 assert(!ipa3_usb_init());assert(!ipa_usb_init_teth_prot(IPA_USB_RNDIS,&params,notify,(void*)1));
 int registrations=register_calls;
 assert(ipa_usb_init_teth_prot(IPA_USB_ECM,&params,different_notify,(void*)2)==-EINVAL&&register_calls==registrations);
 fail_protocol=-EIO;assert(ipa_usb_init_teth_prot(IPA_USB_ECM,&params,notify,(void*)2)==-EIO&&pm_live==1);fail_protocol=0;
 assert(!ipa_usb_init_teth_prot(IPA_USB_ECM,&params,notify,(void*)2));assert(pm_live==1&&ipa3_usb_ctx->num_init_prot==2);
 assert(!ipa_usb_deinit_teth_prot(IPA_USB_RNDIS));assert(pm_live==1&&ipa3_usb_ctx->num_init_prot==1);
 assert(!ipa_usb_deinit_teth_prot(IPA_USB_ECM));clean();
 assert(!ipa3_usb_init());
 for(int p=-5;p<IPA_USB_MAX_TETH_PROT_SIZE+5;p++){if(p>=0&&p<IPA_USB_MAX_TETH_PROT_SIZE)continue;assert(ipa_usb_init_teth_prot(p,&params,notify,(void*)1)==-EINVAL);assert(ipa_usb_deinit_teth_prot(p)==-EINVAL);}
 assert(ipa_usb_init_teth_prot(IPA_USB_RNDIS,NULL,notify,(void*)1)==-EINVAL);assert(ipa_usb_init_teth_prot(IPA_USB_DIAG,NULL,NULL,(void*)1)==-EINVAL);assert(ipa_usb_init_teth_prot(IPA_USB_DIAG,NULL,notify,NULL)==-EINVAL);
 ipa3_usb_ctx->shutting_down=true;assert(ipa_usb_init_teth_prot(IPA_USB_DIAG,NULL,notify,(void*)1)==-ENODEV);assert(ipa_usb_deinit_teth_prot(IPA_USB_DIAG)==-ENODEV);clean();
 for(int i=0;i<100;i++){assert(!ipa3_usb_init());for(int t=0;t<IPA_USB_TRANSPORT_MAX;t++){ipa3_usb_ctx->ttype_ctx[t].ipa_usb_notify_cb=notify;ipa3_usb_ctx->ttype_ctx[t].user_data=(void*)1;ipa3_usb_ctx->ttype_ctx[t].pm_ctx.remote_wakeup_work.pending=1;}clean();cycles++;}
 printf("{\"init_pm_and_protocol_faults\":%u,\"lifecycle_cycles\":%u,\"pm_rollback_handle_retry\":true,\"deinit_failure_preserves_protocol\":true,\"shared_pm_ownership\":true,\"callback_withdrawal_before_queue_drain\":true}\n",faults,cycles);
}
