#include "rmnet_mock.h"
typedef unsigned short umode_t;
struct dentry { int alive; };
/* CTL TYPES */
#define RMNET_CTL_LOG_PAGE 10
#define RMNET_CTL_LOG_NAME "rmnet_ctl"
#define RMNET_CTL_LOG_LVL "ipc_log_lvl"
static DEFINE_MUTEX(client_lock);
static struct rmnet_ctl_endpoint ctl_ep;
static u8 ipc_log_lvl=RMNET_CTL_LOG_DEBUG;
static unsigned logs,debugs,delivered,xmits;
static struct dentry *debugfs_create_dir(const char *n,void *p) { struct dentry *d=kzalloc(sizeof(*d),0);if(d){d->alive=1;debugs++;}return d; }
static void debugfs_create_u8(const char *n,umode_t m,struct dentry *d,u8 *p) { assert(d->alive); }
static void debugfs_remove_recursive(struct dentry *d) { if(!IS_ERR_OR_NULL(d)){assert(d->alive);debugs--;kfree(d);} }
static void *ipc_log_context_create(unsigned n,const char *s,unsigned f) { void *p=kzalloc(8,0);if(p)logs++;return p; }
static void ipc_log_context_destroy(void *p) { assert(!rcu_depth);if(p){logs--;kfree(p);} }
static void ipc_log_string(void *p,const char *fmt,...) { assert(rcu_depth && p); }
void rmnet_ctl_log(enum rmnet_ctl_log_lvl,const char*,int,const void*,unsigned);
#define rmnet_ctl_log_info(m,d,l) rmnet_ctl_log(RMNET_CTL_LOG_INFO,m,0,d,l)
#define rmnet_ctl_log_err(m,e,d,l) rmnet_ctl_log(RMNET_CTL_LOG_ERR,m,e,d,l)
/* FUNCTIONS */
static void receive(struct sk_buff *s) { assert(rcu_depth && s->protocol==htons(ETH_P_MAP));delivered++;kfree_skb(s); }
static int transmit(struct rmnet_ctl_dev *d,struct sk_buff *s) { assert(rcu_depth);xmits++;kfree_skb(s);return 0; }
int main(void)
{
 unsigned char data[16]={1};struct rmnet_ctl_client_hooks hooks={receive};struct rmnet_ctl_dev dev={.xmit=transmit};u64 stats[10];
 assert(!rmnet_ctl_register_client(NULL));alloc_fault=alloc_step+1;assert(!rmnet_ctl_register_client(&hooks));alloc_fault=0;
 assert(rmnet_ctl_unregister_client(NULL)==-EINVAL);assert(rmnet_ctl_send_client(NULL,alloc_skb(4,0))==-EINVAL && !skbs);
 rmnet_ctl_endpoint_post(alloc_skb(4,0),U32_MAX);assert(!skbs && !live);
 for(unsigned i=0;i<100;i++) {
  void *handle=rmnet_ctl_register_client(&hooks);assert(handle && logs==1 && debugs==1);assert(!rmnet_ctl_register_client(&hooks));
  assert(rmnet_ctl_unregister_client((char*)handle+1)==-EINVAL);
  rmnet_ctl_endpoint_post(data,sizeof(data));assert(!skbs);rmnet_ctl_endpoint_post(alloc_skb(8,0),U32_MAX);assert(!skbs);
  rmnet_ctl_endpoint_post(data,(size_t)U32_MAX+1);assert(!skbs);
  assert(rmnet_ctl_send_client(handle,alloc_skb(8,0))==-EINVAL && !skbs);
  rmnet_ctl_endpoint_setdev(&dev);assert(!rmnet_ctl_send_client(handle,alloc_skb(8,0)) && !skbs);
  assert(rmnet_ctl_get_stats(NULL,1)==-EINVAL && rmnet_ctl_get_stats(stats,-1)==-EINVAL && rmnet_ctl_get_stats(stats,10)==5);
  rmnet_ctl_endpoint_setdev(NULL);assert(!rmnet_ctl_get_stats(stats,10));
  assert(!rmnet_ctl_unregister_client(handle));assert(!logs && !debugs && !live && !rcu_depth && !client_lock.held);
 }
 assert(delivered==200 && xmits==100);
 puts("{\"ctl_lifecycle_cycles\":100,\"skb_ownership_checked\":true,\"stale_and_null_clients_rejected\":true,\"log_withdrawal_and_grace_period_before_free\":true,\"statistics_bounds\":true}");
}
