#include "rmnet_mock.h"
#define IPA_RMNET_LL_RECEIVE 1
#define IPA_RMNET_LL_FLOW_EVT 2
#define MAX_Q_LEN 1000
struct net_device { int refs; };
struct rmnet_ll_endpoint { struct net_device *phys_dev; };
/* LL STATS */
static struct rmnet_ll_stats stats;
static struct rmnet_ll_endpoint *rmnet_ll_ipa_ep;
static struct sk_buff_head tx_pending_list;
static DEFINE_SPINLOCK(rmnet_ll_tx_lock);
struct tasklet_struct { bool pending; };
static struct tasklet_struct tx_pending_task;
static int init_net,tx_result,delivered,missing_device,grace_before_remove;
static struct net_device phys;
static struct rmnet_ll_stats *rmnet_ll_get_stats(void) { return &stats; }
static struct net_device *dev_get_by_name(void *net,const char *name) { if(missing_device)return NULL;phys.refs++;return &phys; }
static void dev_put(struct net_device *d) { assert(d==&phys && d->refs>0 && rcu_syncs>grace_before_remove);d->refs--; }
static int ipa_rmnet_ll_xmit(struct sk_buff *s) { assert(rmnet_ll_tx_lock);if(tx_result!=-EAGAIN)kfree_skb(s);return tx_result; }
static void tasklet_schedule(struct tasklet_struct *t) { assert(rmnet_ll_tx_lock && rmnet_ll_ipa_ep);t->pending=true; }
static void tasklet_kill(struct tasklet_struct *t) { assert(!rmnet_ll_tx_lock && !rmnet_ll_ipa_ep && !tx_pending_list.count);t->pending=false; }
static int netif_rx(struct sk_buff *s) { assert(rcu_depth && s->dev==&phys && s->rxqueue==1 && s->priority==0xda1a);delivered++;kfree_skb(s);return 0; }
/* FUNCTIONS */
static int transmit(struct sk_buff *s) { spin_lock_bh(&rmnet_ll_tx_lock);int e=rmnet_ll_ipa_tx(s);spin_unlock_bh(&rmnet_ll_tx_lock);return e; }
int main(void)
{
 __skb_queue_head_init(&tx_pending_list);grace_before_remove=rcu_syncs;rmnet_ll_ipa_remove(NULL);assert(!live);
 alloc_fault=alloc_step+1;rmnet_ll_ipa_probe(NULL);alloc_fault=0;assert(!rmnet_ll_ipa_ep);
 missing_device=1;rmnet_ll_ipa_probe(NULL);missing_device=0;assert(!live && !phys.refs);
 rmnet_ll_ipa_rx((void*)IPA_RMNET_LL_RECEIVE,alloc_skb(4,0));assert(!live);assert(transmit(alloc_skb(4,0))==-ENODEV && !live);
 for(unsigned i=0;i<100;i++) {
  rmnet_ll_ipa_probe(NULL);assert(rmnet_ll_ipa_ep && phys.refs==1);rmnet_ll_ipa_probe(NULL);assert(phys.refs==1);
  rmnet_ll_ipa_rx((void*)IPA_RMNET_LL_RECEIVE,alloc_skb(4,0));
  tx_result=0;assert(!transmit(alloc_skb(4,0)));
  tx_result=-EAGAIN;assert(!transmit(alloc_skb(4,0)) && tx_pending_list.count==1);
  rmnet_ll_ipa_rx((void*)IPA_RMNET_LL_FLOW_EVT,NULL);assert(tx_pending_task.pending);
  grace_before_remove=rcu_syncs;rmnet_ll_ipa_remove(NULL);assert(!rmnet_ll_ipa_ep && !phys.refs && !live && !skbs && !tx_pending_task.pending);
  rmnet_ll_ipa_rx((void*)IPA_RMNET_LL_FLOW_EVT,NULL);assert(!tx_pending_task.pending);
 }
 assert(delivered==100);puts("{\"endpoint_cycles\":100,\"failed_probe_cleanup\":true,\"no_endpoint_rx_tx_release_skb\":true,\"withdrawal_before_tasklet_and_rcu_drain\":true,\"queued_packets_released\":true}");
}
