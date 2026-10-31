#include "ipa_mock.h"
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,33)
struct net_device { bool napi_added; };
struct napi_struct { struct net_device *owner; };
static struct ipa3_context { bool lan_rx_napi_enable, tx_napi_enable; struct net_device *generic_ndev; struct napi_struct napi_lan_rx; } context, *ipa3_ctx=&context;
static unsigned additions, removals;
static struct net_device *alloc_netdev_dummy(unsigned size) { return kzalloc(sizeof(struct net_device),0); }
static void free_netdev(struct net_device *dev) { assert(dev && !dev->napi_added);kfree(dev); }
static int ipa3_lan_poll(struct napi_struct *napi,int budget) { return 0; }
static void netif_napi_add(struct net_device *dev,struct napi_struct *napi,int (*poll)(struct napi_struct *,int)) { assert(dev && !dev->napi_added);dev->napi_added=true;napi->owner=dev;additions++; }
static void netif_napi_del(struct napi_struct *napi) { assert(napi->owner && napi->owner->napi_added);napi->owner->napi_added=false;napi->owner=NULL;removals++; }
/* FUNCTIONS */
int main(void)
{
 unsigned cases=0;
 for(unsigned rx=0;rx<2;rx++) for(unsigned tx=0;tx<2;tx++) {
  context.lan_rx_napi_enable=rx;context.tx_napi_enable=tx;
  alloc_fault=alloc_step+1;
  assert(ipa3_enable_napi_netdev()==((rx||tx)?-ENOMEM:0));
  assert(!context.generic_ndev);ipa3_disable_napi_netdev();assert(!live);cases++;
  alloc_fault=0;assert(!ipa3_enable_napi_netdev());assert(!!context.generic_ndev==(rx||tx));
  ipa3_disable_napi_netdev();assert(!context.generic_ndev && !live && additions==removals);
  ipa3_disable_napi_netdev();cases++;
 }
 printf("{\"rx_tx_allocation_cleanup_cases\":%u,\"dummy_netdev_ownership_balanced\":true}\n",cases);
}
