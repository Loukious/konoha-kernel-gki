#include "ipa_mock.h"
enum ipa_usb_notify_event { IPA_USB_DEVICE_READY, IPA_USB_REMOTE_WAKEUP };
enum ipa3_usb_transport_type { IPA_USB_TRANSPORT_TETH, IPA_USB_TRANSPORT_TETH_2, IPA_USB_TRANSPORT_DPL, IPA_USB_TRANSPORT_MAX };
enum ipa3_usb_state { IPA_USB_INVALID, IPA_USB_SUSPENDED, IPA_USB_INITIALIZED };
struct ipa3_usb_pm_context { struct work_struct remote_wakeup_work; };
struct ipa3_usb_transport_type_ctx { struct ipa3_usb_pm_context pm_ctx; int (*ipa_usb_notify_cb)(enum ipa_usb_notify_event,void*); void *user_data; enum ipa3_usb_state state; bool rwakeup_pending; };
struct ipa3_usb_context { spinlock_t state_lock,notify_lock; bool shutting_down; struct ipa3_usb_transport_type_ctx ttype_ctx[IPA_USB_TRANSPORT_MAX]; };
static struct ipa3_usb_context *ipa3_usb_ctx;
static void mock_log(const char*format,...){}
static const char*ipa3_usb_notify_event_to_string(enum ipa_usb_notify_event e){return "event";}
#define IPA_USB_DBG(...) mock_log(__VA_ARGS__)
#define IPA3_USB_IS_TTYPE_DPL(t) ((t)==IPA_USB_TRANSPORT_DPL)
struct f_gsi { struct kref ref; struct gsi_ipa_token *ipa_token; int calls; };
struct gsi_ipa_token { spinlock_t lock; struct f_gsi *gsi; };
static void release_gsi(struct kref *r) { kfree(container_of(r,struct f_gsi,ref)); }
static void gsi_put(struct f_gsi *g) { kref_put(&g->ref,release_gsi); }
static int ipa_usb_notify_gsi(enum ipa_usb_notify_event event,void *data) { struct f_gsi *g=data; g->calls++; return 1; }
/* FUNCTIONS */
int main(void)
{
    struct ipa3_usb_context ctx={0}; ipa3_usb_ctx=&ctx;
    struct f_gsi *instances[3]; struct gsi_ipa_token tokens[3]={0};
    for(int i=0;i<3;i++){ instances[i]=kzalloc(sizeof(*instances[i]),0);kref_init(&instances[i]->ref);instances[i]->ipa_token=&tokens[i];gsi_ipa_token_activate(instances[i]);ctx.ttype_ctx[i].ipa_usb_notify_cb=ipa_usb_notify_cb;ipa3_usb_set_notify_data(i,&tokens[i]);ctx.ttype_ctx[i].state=IPA_USB_SUSPENDED; }
    for(int i=0;i<3;i++){ipa3_usb_wq_notify_remote_wakeup(&ctx.ttype_ctx[i].pm_ctx.remote_wakeup_work);assert(instances[i]->calls==1);for(int j=i+1;j<3;j++)assert(!instances[j]->calls);ipa3_usb_wq_notify_remote_wakeup(&ctx.ttype_ctx[i].pm_ctx.remote_wakeup_work);assert(instances[i]->calls==1);}
    ipa3_usb_reset_notify(0);ipa3_usb_notify_do(0,IPA_USB_DEVICE_READY);assert(instances[0]->calls==1);
    ctx.ttype_ctx[1].state=IPA_USB_INVALID;ctx.ttype_ctx[1].rwakeup_pending=false;ipa3_usb_wq_notify_remote_wakeup(&ctx.ttype_ctx[1].pm_ctx.remote_wakeup_work);assert(instances[1]->calls==1);
    ctx.shutting_down=true;ipa3_usb_notify_do(2,IPA_USB_DEVICE_READY);assert(instances[2]->calls==1);
    for(int i=0;i<3;i++){gsi_ipa_token_retire(instances[i]);gsi_put(instances[i]);gsi_put(instances[i]);}
    for(int i=0;i<1000;i++)for(int j=0;j<3;j++)assert(ipa_usb_notify_cb(IPA_USB_REMOTE_WAKEUP,&tokens[j])==-ENODEV);
    assert(!live);puts("{\"three_transport_wakeups_isolated\":true,\"duplicate_wakeups_suppressed\":true,\"shutdown_closes_notifications\":true,\"late_retired_token_callbacks\":3000}");
}
