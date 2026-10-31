#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#define EPROBE_DEFER 517
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((intptr_t)(p))
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define pr_debug(...) ((void)0)
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
static void list_add_tail(struct list_head *n,struct list_head *h){n->next=h;n->prev=h->prev;h->prev->next=n;h->prev=n;}
static void list_del_init(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;n->next=n->prev=n;}
struct device_node {int refs;bool available;struct device_node *target;};
static struct device_node *of_parse_phandle(const struct device_node *n,const char *name,int i){struct device_node *r=n?n->target:NULL;if(r)r->refs++;return r;}
static bool of_device_is_available(struct device_node *n){return n&&n->available;}
static void of_node_put(struct device_node *n){if(n){assert(n->refs>1);n->refs--;}}
#define spin_lock(l) assert(!pthread_mutex_lock(l))
#define spin_unlock(l) assert(!pthread_mutex_unlock(l))
#define mutex_lock(l) assert(!pthread_mutex_lock(l))
#define mutex_unlock(l) assert(!pthread_mutex_unlock(l))
static LIST_HEAD(usb_redriver_list);
static pthread_mutex_t usb_redriver_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t usb_redriver_unbind_lock=PTHREAD_MUTEX_INITIALIZER;
/* TYPES */
/* FUNCTIONS */
static unsigned unbind_count,forward_count;
static void unbind(struct usb_redriver *r){assert(r->bounded);unbind_count++;}
static int noarg(struct usb_redriver *r){assert(r->bounded);forward_count++;return 0;}
static int onearg(struct usb_redriver *r,int v){assert(r->bounded&&v==1);forward_count++;return 0;}
static int twoarg(struct usb_redriver *r,int v,int n){assert(r->bounded&&v==1&&n==2);forward_count++;return 0;}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static int entered,released,callback_done,remove_done;
static void blocking_unbind(struct usb_redriver *r){mutex_lock(&gate);entered=1;pthread_cond_broadcast(&cv);while(!released)pthread_cond_wait(&cv,&gate);assert(r->bounded);callback_done=1;mutex_unlock(&gate);}
static void *put_thread(void *p){usb_put_redriver(p);return NULL;}
static void *remove_thread(void *p){assert(!usb_remove_redriver(p));mutex_lock(&gate);assert(callback_done);remove_done=1;mutex_unlock(&gate);return NULL;}
int main(void){
 struct device_node nodes[4],consumer={0};struct usb_redriver drivers[8];bool registered[8]={0},bound[8]={0};unsigned model_cases=0;uint32_t random=0x2eaa;
 memset(nodes,0,sizeof(nodes));memset(drivers,0,sizeof(drivers));for(int i=0;i<4;i++){nodes[i].refs=1;nodes[i].available=true;}
 for(int i=0;i<8;i++){drivers[i].of_node=&nodes[i%4];drivers[i].unbind=unbind;}
 assert(usb_add_redriver(NULL)==-EINVAL);assert(usb_remove_redriver(NULL)==-EINVAL);assert(!usb_get_redriver_by_phandle(&consumer,"r",0));
 consumer.target=&nodes[0];nodes[0].available=false;assert(!usb_get_redriver_by_phandle(&consumer,"r",0));assert(nodes[0].refs==1);nodes[0].available=true;
 for(unsigned step=0;step<10000;step++){
  random=random*1664525+1013904223;unsigned i=(random>>16)%8,op=(random>>24)%4;int owner=-1;for(unsigned j=0;j<8;j++)if(registered[j]&&drivers[j].of_node==drivers[i].of_node)owner=j;
  if(op==0){int ret=usb_add_redriver(&drivers[i]);assert(ret==(owner>=0?-EEXIST:0));if(!ret)registered[i]=true;}
  if(op==1){int expected=!registered[i]?-ENOENT:bound[i]?-EBUSY:0;assert(usb_remove_redriver(&drivers[i])==expected);if(!expected)registered[i]=false;}
  if(op==2){consumer.target=drivers[i].of_node;struct usb_redriver *r=usb_get_redriver_by_phandle(&consumer,"r",0);if(owner<0)assert(PTR_ERR(r)==-EPROBE_DEFER);else if(bound[owner])assert(PTR_ERR(r)==-EBUSY);else{assert(r==&drivers[owner]);bound[owner]=true;}}
  if(op==3){unsigned old=unbind_count;usb_put_redriver(&drivers[i]);assert(unbind_count==old+(registered[i]&&bound[i]));bound[i]=false;}
  for(unsigned j=0;j<4;j++){int b=0;for(unsigned k=0;k<8;k++)if(bound[k]&&drivers[k].of_node==&nodes[j])b++;assert(nodes[j].refs==1+b);}model_cases++;
 }
 for(unsigned i=0;i<8;i++){usb_put_redriver(&drivers[i]);if(registered[i])assert(!usb_remove_redriver(&drivers[i]));}
 struct usb_redriver *r=&drivers[0];r->release_usb_lanes=twoarg;r->notify_connect=onearg;r->notify_disconnect=noarg;r->gadget_pullup_enter=onearg;r->gadget_pullup_exit=onearg;r->host_powercycle=noarg;assert(!usb_add_redriver(r));consumer.target=r->of_node;assert(usb_get_redriver_by_phandle(&consumer,"r",0)==r);
 usb_redriver_release_lanes(r,1,2);usb_redriver_notify_connect(r,1);usb_redriver_notify_disconnect(r);usb_redriver_gadget_pullup_enter(r,1);usb_redriver_gadget_pullup_exit(r,1);usb_redriver_host_powercycle(r);assert(forward_count==6);
 r->unbind=blocking_unbind;pthread_t putter,remover;assert(!pthread_create(&putter,NULL,put_thread,r));mutex_lock(&gate);while(!entered)pthread_cond_wait(&cv,&gate);mutex_unlock(&gate);assert(!pthread_create(&remover,NULL,remove_thread,r));mutex_lock(&gate);released=1;pthread_cond_broadcast(&cv);mutex_unlock(&gate);pthread_join(putter,NULL);pthread_join(remover,NULL);assert(remove_done&&callback_done);assert(nodes[0].refs==1);usb_put_redriver(r);
 printf("{\"registry_model_cases\":%u,\"forwarded_callbacks\":6,\"concurrent_unbind_remove_cases\":1}\n",model_cases);
}
