#include "gsi_mock.h"
#define ETH_ALEN 6
struct sg_table{int unused;};
/* IPA_TYPES */
struct gsi_ipa_module{struct list_head list;struct module*mod;};
struct gsi_ipa_token{struct list_head list;spinlock_t lock;struct f_gsi*gsi;struct module*owner;};
static LIST_HEAD(ipa_tokens);static LIST_HEAD(ipa_modules);static int ipa_ops_lock;static struct ipa_usb_ops ipa_ops;static struct module*ipa_owner;static struct completion wait_for_ipa_ready;
struct f_gsi{struct gsi_ipa_token*ipa_token;struct module*ipa_owner;struct ipa_usb_ops ipa_ops;struct{struct ipa_usb_ops*ipa_ops;}d_port;};
/* FUNCTIONS */
static int init(enum ipa_usb_teth_prot p,struct ipa_usb_teth_params*t,int(*n)(enum ipa_usb_notify_event,void*),void*d){return 0;}
static int connect_op(struct ipa_usb_xdci_chan_params*a,struct ipa_usb_xdci_chan_params*b,struct ipa_req_chan_out_params*c,struct ipa_req_chan_out_params*d,struct ipa_usb_xdci_connect_params*e){return 0;}
static int disconnect(u32 a,u32 b,enum ipa_usb_teth_prot c){return 0;}
static int deinit(enum ipa_usb_teth_prot p){return 0;}
static int suspend_op(u32 a,u32 b,enum ipa_usb_teth_prot c,bool d){return 0;}
static bool connected(enum ipa_usb_teth_prot p){return true;}
int main(void){struct ipa_usb_ops ops={init,connect_op,disconnect,deinit,suspend_op,disconnect,connected};struct module m={.state=MODULE_STATE_COMING,.mem={{(void*)1,SIZE_MAX-1}}};struct f_gsi g={0};assert(!gsi_ipa_ops_valid(NULL));for(int field=0;field<7;field++){struct ipa_usb_ops bad=ops;memset((char*)&bad+field*sizeof(void*),0,sizeof(void*));assert(!gsi_ipa_ops_valid(&bad));ipa_ready_callback(&bad);assert(!ipa_owner);}
assert(!gsi_ipa_module_notify(NULL,MODULE_STATE_COMING,&m));assert(live==1);ipa_ready_callback(&ops);assert(ipa_owner==&m&&wait_for_ipa_ready.done);assert(gsi_ipa_get(&g)==-ENODEV&&!m.refs);m.state=MODULE_STATE_LIVE;assert(!gsi_ipa_get(&g)&&m.refs==1&&g.d_port.ipa_ops==&g.ipa_ops);assert(g.ipa_ops.xdci_connect==connect_op);struct ipa_usb_ops changed=ops;changed.xdci_connect=NULL;ipa_ready_callback(&changed);assert(g.ipa_ops.xdci_connect==connect_op);gsi_ipa_put(&g);assert(!m.refs&&!g.ipa_owner&&!g.d_port.ipa_ops);m.state=MODULE_STATE_GOING;gsi_ipa_module_notify(NULL,MODULE_STATE_GOING,&m);assert(!ipa_owner&&!wait_for_ipa_ready.done&&!live);assert(gsi_ipa_get(&g)==-ENODEV);alloc_fault=alloc_step+1;assert(!gsi_ipa_module_notify(NULL,MODULE_STATE_COMING,&m));ipa_ready_callback(&ops);assert(!ipa_owner&&!live);alloc_fault=0;gsi_ipa_registry_clear();printf("{\"malformed_ops_fields\":7,\"coming_provider_refused\":true,\"live_binding_pins_provider\":true,\"going_revokes_unbound_ops\":true,\"registry_allocation_failure_safe\":true}\n");}
