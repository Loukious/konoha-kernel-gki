#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sched.h>
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define EXTCON_USB 1
#define EXTCON_USB_HOST 2
#define NOTIFY_DONE 0
#define dev_err(...) ((void)0)
#define __rcu
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define ERR_PTR(n) ((void*)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define rcu_assign_pointer(p,v) atomic_store_explicit(&(p),(v),memory_order_release)
#define srcu_dereference(p,s) atomic_load_explicit(&(p),memory_order_acquire)
struct dwc3_msm;
struct notifier_block { int (*notifier_call)(struct notifier_block*,unsigned long,void*); };
struct extcon_dev { int id; const char *name; };
struct extcon_nb { struct extcon_dev *edev; _Atomic(struct dwc3_msm*) mdwc; int idx; struct notifier_block vbus_nb,id_nb; bool is_eud,vbus_registered,id_registered; };
struct device_node { int count; };
struct device { struct device_node *of_node; };
struct dwc3_msm { struct device *dev; struct extcon_nb *extcon; unsigned extcon_count; bool extcon_published; };
static pthread_rwlock_t dwc3_msm_extcon_srcu=PTHREAD_RWLOCK_INITIALIZER;
static atomic_bool synchronize_entered,callback_entered,release_callback,unregister_done;
static bool block_callback;
static atomic_int event_calls;
static int active[4][3],unreg_order[8],unreg_count;
static int register_fail,register_errno,register_step,missing_source,missing_errno;
static struct extcon_dev providers[4]={{0,"usb0"},{1,"eud1"},{2,"usb2"},{3,"usb3"}};
static int srcu_read_lock(pthread_rwlock_t *s){assert(!pthread_rwlock_rdlock(s));return 0;}
static void srcu_read_unlock(pthread_rwlock_t *s,int i){assert(!pthread_rwlock_unlock(s));}
static void synchronize_srcu(pthread_rwlock_t *s){atomic_store(&synchronize_entered,true);assert(!pthread_rwlock_wrlock(s));assert(!pthread_rwlock_unlock(s));}
static int do_event(void){atomic_fetch_add(&event_calls,1);if(block_callback){atomic_store(&callback_entered,true);while(!atomic_load(&release_callback))sched_yield();}return 7;}
static int dwc3_msm_id_event(struct extcon_nb *e,struct dwc3_msm *m,unsigned long event,void *p){assert(m);return do_event();}
static int dwc3_msm_vbus_event(struct extcon_nb *e,struct dwc3_msm *m,unsigned long event,void *p){assert(m);return do_event();}
static int of_count_phandle_with_args(struct device_node *n,const char *p,void *a){return n->count;}
#define kcalloc(n,s,gfp) calloc(n,s)
static struct extcon_dev *extcon_get_edev_by_phandle(struct device *d,int i){return i==missing_source?ERR_PTR(missing_errno):&providers[i];}
static const char *extcon_get_edev_name(struct extcon_dev *e){return e->name;}
static char *strnstr(const char *s,const char *needle,size_t n){return strstr(s,needle);}
static int extcon_register_notifier(struct extcon_dev *e,int cable,struct notifier_block *nb){if(register_step++==register_fail)return register_errno;assert(!active[e->id][cable]);active[e->id][cable]=1;return 0;}
static int extcon_unregister_notifier(struct extcon_dev *e,int cable,struct notifier_block *nb){assert(active[e->id][cable]);active[e->id][cable]=0;unreg_order[unreg_count++]=e->id*3+cable;return 0;}
static int extcon_get_state(struct extcon_dev *e,int cable){return cable==EXTCON_USB;}
static int pin_calls;
static void dwc3_msm_pin_module(void){pin_calls++;}
/* FUNCTIONS */
static void *reader(void *p){struct dwc3_msm *m=p;assert(dwc3_msm_vbus_notifier(&m->extcon[0].vbus_nb,1,&providers[0])==7);return NULL;}
static void *remover(void *p){dwc3_msm_extcon_unregister(p);atomic_store(&unregister_done,true);return NULL;}
int main(void){unsigned cases=0,late_cases=1;
 for(int count=1;count<=4;count++)for(int missing=-1;missing<count;missing++)for(int fail=-1;fail<count*2;fail++)for(int kind=0;kind<3;kind++){
  struct device_node n={count};struct device d={&n};struct dwc3_msm m={.dev=&d};missing_source=missing;missing_errno=kind==2?-EPROBE_DEFER:-ENODEV;register_fail=fail;register_errno=kind==0?-EIO:-EINVAL;register_step=0;unreg_count=0;pin_calls=0;memset(active,0,sizeof(active));
  int ret=dwc3_msm_extcon_register(&m);
  if(missing>=0 && kind==2 && (ret==-EPROBE_DEFER||ret==-EINVAL))assert(ret==-EPROBE_DEFER);
  if(!ret)assert(m.extcon_count==(unsigned)count);
  dwc3_msm_extcon_unregister(&m);for(int i=0;i<4;i++)for(int j=0;j<3;j++)assert(!active[i][j]);for(int i=1;i<unreg_count;i++)assert(unreg_order[i-1]>unreg_order[i]);
  for(int i=0;i<count;i++){late_cases+=2;assert(!atomic_load(&m.extcon[i].mdwc));assert(dwc3_msm_id_notifier(&m.extcon[i].id_nb,1,&providers[i])==NOTIFY_DONE);assert(dwc3_msm_vbus_notifier(&m.extcon[i].vbus_nb,1,&providers[i])==NOTIFY_DONE);}
  dwc3_msm_extcon_unregister(&m);free(m.extcon);cases++;
 }
 struct device_node n={1};struct device d={&n};struct dwc3_msm m={.dev=&d};missing_source=-1;register_fail=-1;register_step=0;unreg_count=0;assert(!dwc3_msm_extcon_register(&m));block_callback=true;atomic_store(&synchronize_entered,false);pthread_t a,b;assert(!pthread_create(&a,NULL,reader,&m));while(!atomic_load(&callback_entered))sched_yield();assert(!pthread_create(&b,NULL,remover,&m));while(!atomic_load(&synchronize_entered))sched_yield();assert(!atomic_load(&unregister_done));atomic_store(&release_callback,true);pthread_join(a,NULL);pthread_join(b,NULL);assert(atomic_load(&unregister_done));assert(dwc3_msm_vbus_notifier(&m.extcon[0].vbus_nb,1,&providers[0])==NOTIFY_DONE);free(m.extcon);
 printf("{\"extcon_registration_unwind_cases\":%u,\"callback_drain_cases\":1,\"late_callback_cases\":%u}\n",cases,late_cases);
}
