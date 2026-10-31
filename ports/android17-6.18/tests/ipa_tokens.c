#include "gsi_mock.h"
enum ipa_usb_notify_event{IPA_USB_DEVICE_READY};
struct f_gsi{struct kref ref;struct gsi_ipa_token*ipa_token;int calls;};
struct gsi_ipa_token{struct list_head list;spinlock_t lock;struct f_gsi*gsi;struct module*owner;};
static int freed;
static void release(struct kref*r){freed++;kfree(container_of(r,struct f_gsi,ref));}
static void gsi_put(struct f_gsi*g){kref_put(&g->ref,release);}
static int ipa_usb_notify_gsi(enum ipa_usb_notify_event e,void*p){struct f_gsi*g=p;g->calls++;return 1;}
/* FUNCTIONS */
int main(void){struct gsi_ipa_token old={0},fresh={0};struct f_gsi*g=kzalloc(sizeof(*g),0);kref_init(&g->ref);g->ipa_token=&old;gsi_ipa_token_activate(g);assert(g->ref.refs==2);assert(ipa_usb_notify_cb(IPA_USB_DEVICE_READY,&old)==1&&g->calls==1&&g->ref.refs==2);gsi_ipa_token_retire(g);gsi_put(g);gsi_put(g);assert(freed==1&&!live);assert(ipa_usb_notify_cb(IPA_USB_DEVICE_READY,&old)==-ENODEV);g=kzalloc(sizeof(*g),0);kref_init(&g->ref);g->ipa_token=&fresh;gsi_ipa_token_activate(g);for(int i=0;i<1000;i++)assert(ipa_usb_notify_cb(IPA_USB_DEVICE_READY,&old)==-ENODEV);assert(!g->calls);assert(ipa_usb_notify_cb(IPA_USB_DEVICE_READY,&fresh)==1&&g->calls==1);gsi_ipa_token_retire(g);gsi_put(g);gsi_put(g);assert(freed==2&&!live);printf("{\"retired_callbacks_after_free_and_rebind\":1001,\"distinct_bind_token_generation\":true,\"callback_takes_context_reference\":true}\n");}
