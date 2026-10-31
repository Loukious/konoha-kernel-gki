#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define GFP_KERNEL 0
#define KBUILD_MODNAME "test"
#define THERMAL_NAME_LENGTH 20
#define CPUHP_AP_ONLINE_DYN 100
#define EPROBE_DEFER 517
#define S32_MAX INT_MAX
#define THERMAL_TRIP_VIOLATED 1
#define PM_HIBERNATION_PREPARE 1
#define PM_RESTORE_PREPARE 2
#define PM_SUSPEND_PREPARE 3
#define PM_POST_HIBERNATION 4
#define PM_POST_RESTORE 5
#define PM_POST_SUSPEND 6
#define NOTIFY_OK 1
#define AF_QIPCRTR 42
#define QMI_RESULT_SUCCESS_V01 0
#define QMI_INDICATION 4
#define DL_FLAG_AUTOREMOVE_CONSUMER 1
#define FREQ_QOS_MAX 1
#define DEV_PM_QOS_MAX_FREQUENCY 1
#define PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE INT_MAX
#define CPUFREQ_ENTRY_INVALID UINT_MAX
#define CPUFREQ_TABLE_END (UINT_MAX-1)
#define CPUFREQ_TABLE_SORTED_ASCENDING 1
#define CPUFREQ_TABLE_SORTED_DESCENDING 2
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define min(a,b) ((a)<(b)?(a):(b))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define msecs_to_jiffies(x) (x)
#define system_highpri_wq NULL
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
typedef atomic_int atomic_t;
#define atomic_set(p,v) atomic_store(p,v)
#define atomic_read(p) atomic_load(p)
struct mutex { pthread_mutex_t m; };
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static _Thread_local struct mutex *held[16];static _Thread_local int held_count,spins;
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->m,NULL));}
static void mutex_lock(struct mutex *m){for(int i=0;i<held_count;++i)assert(held[i]!=m);assert(!spins);assert(!pthread_mutex_lock(&m->m));held[held_count++]=m;}
static void mutex_unlock(struct mutex *m){assert(held_count&&held[held_count-1]==m);--held_count;assert(!pthread_mutex_unlock(&m->m));}
typedef pthread_mutex_t spinlock_t;
static void spin_lock_init(spinlock_t *l){assert(!pthread_mutex_init(l,NULL));}
static void spin_lock(spinlock_t *l){assert(!pthread_mutex_lock(l));++spins;}
static void spin_unlock(spinlock_t *l){--spins;assert(!pthread_mutex_unlock(l));}
struct list_head{struct list_head *next,*prev;};
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
#define LIST_HEAD(n) struct list_head n={&n,&n}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_add(struct list_head *n,struct list_head *h){list_add_tail(n,h);}
static bool list_empty(struct list_head *h){return h->next==h;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
struct work_struct{void(*fn)(struct work_struct *);pthread_mutex_t lock;pthread_cond_t cv;bool pending,running;};
struct delayed_work{struct work_struct work;};
static int work_pending;
static void init_work(struct work_struct*w,void(*fn)(struct work_struct*)){w->fn=fn;pthread_mutex_init(&w->lock,NULL);pthread_cond_init(&w->cv,NULL);}
#define INIT_WORK(w,f) init_work(w,f)
#define INIT_DEFERRABLE_WORK(w,f) init_work(&(w)->work,f)
static bool queue_work(void *q,struct work_struct*w){bool ret;pthread_mutex_lock(&w->lock);ret=!w->pending;if(ret){w->pending=true;++work_pending;}pthread_mutex_unlock(&w->lock);return ret;}
static bool queue_delayed_work(void*q,struct delayed_work*w,int delay){return queue_work(q,&w->work);}
static void cancel_work_sync(struct work_struct*w){assert(!held_count&&!spins);pthread_mutex_lock(&w->lock);if(w->pending){w->pending=false;--work_pending;}while(w->running)pthread_cond_wait(&w->cv,&w->lock);if(w->pending){w->pending=false;--work_pending;}pthread_mutex_unlock(&w->lock);}
static void cancel_delayed_work_sync(struct delayed_work*w){cancel_work_sync(&w->work);}
static void run_work(struct work_struct*w){pthread_mutex_lock(&w->lock);assert(w->pending&&!w->running);w->pending=false;--work_pending;w->running=true;pthread_mutex_unlock(&w->lock);w->fn(w);pthread_mutex_lock(&w->lock);w->running=false;pthread_cond_broadcast(&w->cv);pthread_mutex_unlock(&w->lock);}
struct device_node{const char *name;struct device_node *children[8],*phandles[8];int children_count,phandle_count,refs;const char *names[100];int names_count;u32 instance,max_level;bool missing_property;};
static void of_node_put(struct device_node*n){if(n)assert(--n->refs>=1);}
static struct device_node *of_node_get(struct device_node*n){if(n)++n->refs;return n;}
static struct device_node *next_child(struct device_node*n,struct device_node*prev){int i=0;if(prev){while(i<n->children_count&&n->children[i]!=prev)++i;++i;of_node_put(prev);}return i<n->children_count?of_node_get(n->children[i]):NULL;}
#define for_each_available_child_of_node(n,c) for(c=next_child(n,NULL);c;c=next_child(n,c))
static int of_get_available_child_count(struct device_node*n){return n->children_count;}
static struct device_node *of_parse_phandle(struct device_node*n,const char*p,int index){return index<n->phandle_count?of_node_get(n->phandles[index]):NULL;}
static int of_property_read_u32(struct device_node*n,const char*p,u32*out){if(n->missing_property)return -EINVAL;*out=strstr(p,"max-level")?n->max_level:n->instance;return 0;}
static int of_property_count_strings(struct device_node*n,const char*p){return n->missing_property?-EINVAL:n->names_count;}
static int of_property_read_string_index(struct device_node*n,const char*p,int i,const char**out){if(n->missing_property||i>=n->names_count)return -EINVAL;*out=n->names[i];return 0;}
static int of_property_read_string(struct device_node*n,const char*p,const char**out){return of_property_read_string_index(n,p,0,out);}
static int strscpy(char*d,const char*s,size_t n){size_t z=strlen(s);if(z>=n){if(n){memcpy(d,s,n-1);d[n-1]=0;}return -E2BIG;}memcpy(d,s,z+1);return z;}
struct device{struct device_node *of_node;void*data;void*alloc[128];void(*actions[128])(void*);void*action_args[128];int nact,nalloc,refs;};
struct platform_device{struct device dev;};
static int fail_action,fail_alloc,alloc_calls,alloc_live;
static void*kzalloc(size_t n,int flags){if(++alloc_calls==fail_alloc)return NULL;void*p=calloc(1,n);if(p)++alloc_live;return p;}
static void*kcalloc(size_t n,size_t z,int flags){if(n>10000)return NULL;return kzalloc(n*z,flags);}
#define kmalloc_array kcalloc
static void kfree(void*p){if(p){--alloc_live;free(p);}}
static void*devm_kzalloc(struct device*d,size_t n,int flags){void*p=kzalloc(n,flags);if(p)d->alloc[d->nalloc++]=p;return p;}
static void*devm_kcalloc(struct device*d,size_t n,size_t z,int flags){return devm_kzalloc(d,n*z,flags);}
static int devm_add_action_or_reset(struct device*d,void(*fn)(void*),void*arg){if(fail_action){fn(arg);return -ENOMEM;}d->actions[d->nact]=fn;d->action_args[d->nact++]=arg;return 0;}
static void platform_set_drvdata(struct platform_device*p,void*d){p->dev.data=d;}
static void*platform_get_drvdata(struct platform_device*p){return p->dev.data;}
static struct device*get_device(struct device*d){++d->refs;return d;}
static void put_device(struct device*d){assert(--d->refs>=1);}
static struct platform_device gpu_supplier;
static int supplier_missing,link_failure;
static struct platform_device*of_find_device_by_node(struct device_node*n){if(supplier_missing)return NULL;get_device(&gpu_supplier.dev);return &gpu_supplier;}
static void*device_link_add(struct device*c,struct device*s,int flags){return link_failure?NULL:(void*)1;}
static void sort(void*p,size_t n,size_t z,int(*cmp)(const void*,const void*),void*swap){qsort(p,n,z,cmp);}
struct freq_qos_request{bool active;int value;};
struct dev_pm_qos_request{bool active;int value;};
static int qos_add_error,qos_update_error,qos_live,qos_updates;
static bool freq_qos_request_active(struct freq_qos_request*r){return r->active;}
static bool dev_pm_qos_request_active(struct dev_pm_qos_request*r){return r->active;}
static int freq_qos_add_request(void*p,struct freq_qos_request*r,int t,int v){if(qos_add_error)return qos_add_error;assert(!r->active);r->active=true;r->value=v;++qos_live;return 1;}
static int freq_qos_update_request(struct freq_qos_request*r,int v){assert(r->active);++qos_updates;if(qos_update_error)return qos_update_error;r->value=v;return 1;}
static int freq_qos_remove_request(struct freq_qos_request*r){assert(r->active);r->active=false;--qos_live;return 0;}
static int dev_pm_qos_add_request(struct device*d,struct dev_pm_qos_request*r,int t,int v){return freq_qos_add_request(d,(void*)r,t,v);}
static int dev_pm_qos_update_request(struct dev_pm_qos_request*r,int v){return freq_qos_update_request((void*)r,v);}
static int dev_pm_qos_remove_request(struct dev_pm_qos_request*r){return freq_qos_remove_request((void*)r);}
struct thermal_cooling_device;
struct thermal_cooling_device_ops{int(*set_cur_state)(struct thermal_cooling_device*,unsigned long);int(*get_cur_state)(struct thermal_cooling_device*,unsigned long*);int(*get_max_state)(struct thermal_cooling_device*,unsigned long*);};
struct thermal_cooling_device{pthread_mutex_t core_lock;void*devdata;const char*type;const struct thermal_cooling_device_ops*ops;unsigned long max_state;struct device*owner;};
struct thermal_zone_device;
struct thermal_zone_device_ops{int(*get_temp)(struct thermal_zone_device*,int*);int(*set_trips)(struct thermal_zone_device*,int,int);void*change_mode;void*get_trend;};
struct thermal_zone_device{pthread_mutex_t core_lock;void*devdata;struct device*owner;const struct thermal_zone_device_ops*ops;};
#define qti_tz_change_mode NULL
#define qti_tz_get_trend NULL
static struct thermal_cooling_device*cdevs[128];static int cdev_live,cdev_calls,fail_cdev;
static struct thermal_zone_device*zones[128];static int zone_live,zone_calls,fail_zone,notifications;
static struct thermal_cooling_device*thermal_cooling_device_register(const char*n,void*d,const struct thermal_cooling_device_ops*ops){if(++cdev_calls==fail_cdev)return ERR_PTR(-EIO);struct thermal_cooling_device*c=calloc(1,sizeof(*c));pthread_mutex_init(&c->core_lock,NULL);c->devdata=d;c->type=n;c->ops=ops;unsigned long max;assert(!ops->get_max_state(c,&max));c->max_state=max;for(int i=0;i<128;++i)if(!cdevs[i]){cdevs[i]=c;break;}++cdev_live;return c;}
static struct thermal_cooling_device*thermal_of_cooling_device_register(struct device_node*np,const char*n,void*d,const struct thermal_cooling_device_ops*ops){assert(np->refs>=1);return thermal_cooling_device_register(n,d,ops);}
static struct thermal_cooling_device*devm_thermal_of_cooling_device_register(struct device*dev,struct device_node*np,const char*n,void*d,const struct thermal_cooling_device_ops*ops){struct thermal_cooling_device*c=thermal_of_cooling_device_register(np,n,d,ops);if(!IS_ERR(c))c->owner=dev;return c;}
static void thermal_cooling_device_update(struct thermal_cooling_device*c){assert(!held_count&&!spins);assert(!c->ops->get_max_state(c,&c->max_state));}
static void thermal_cooling_device_unregister(struct thermal_cooling_device*c){assert(!held_count&&!spins);pthread_mutex_lock(&c->core_lock);pthread_mutex_unlock(&c->core_lock);for(int i=0;i<128;++i)if(cdevs[i]==c)cdevs[i]=NULL;--cdev_live;free(c);}
static struct thermal_zone_device*devm_thermal_of_zone_register(struct device*d,int id,void*priv,const struct thermal_zone_device_ops*ops){if(++zone_calls==fail_zone)return ERR_PTR(-ENODEV);struct thermal_zone_device*t=calloc(1,sizeof(*t));pthread_mutex_init(&t->core_lock,NULL);t->owner=d;t->devdata=priv;t->ops=ops;for(int i=0;i<128;++i)if(!zones[i]){zones[i]=t;break;}++zone_live;return t;}
static void devm_thermal_of_zone_unregister(struct device*d,struct thermal_zone_device*t){assert(!held_count&&!spins);pthread_mutex_lock(&t->core_lock);pthread_mutex_unlock(&t->core_lock);for(int i=0;i<128;++i)if(zones[i]==t)zones[i]=NULL;--zone_live;free(t);}
static void*thermal_zone_device_priv(struct thermal_zone_device*t){return t->devdata;}
static void thermal_zone_device_update(struct thermal_zone_device*t,int ev){assert(t);++notifications;}
static void free_devres(struct device*d){for(int i=0;i<128;++i){if(cdevs[i]&&cdevs[i]->owner==d)thermal_cooling_device_unregister(cdevs[i]);if(zones[i]&&zones[i]->owner==d)devm_thermal_of_zone_unregister(d,zones[i]);}while(d->nact){--d->nact;d->actions[d->nact](d->action_args[d->nact]);}while(d->nalloc)kfree(d->alloc[--d->nalloc]);d->data=NULL;}
struct cpufreq_frequency_table{unsigned int frequency;};
struct cpufreq_policy{struct cpufreq_frequency_table*freq_table;int freq_table_sorted,constraints,refs;};
static struct cpufreq_policy policy;
static int policy_missing,table_count_override=-1,hp_error,hp_live;
static int(*hp_callback)(unsigned);
static struct device cpus[8];static unsigned nr_cpu_ids=8;
#define for_each_possible_cpu(c) for(c=0;c<(int)nr_cpu_ids;++c)
#define cpufreq_for_each_valid_entry(p,t) for(p=(t);p->frequency!=CPUFREQ_TABLE_END;++p) if(p->frequency!=CPUFREQ_ENTRY_INVALID)
static struct device*get_cpu_device(unsigned i){return &cpus[i];}
static struct cpufreq_policy*cpufreq_cpu_get(int c){if(policy_missing)return NULL;++policy.refs;return &policy;}
static void cpufreq_cpu_put(struct cpufreq_policy*p){assert(p->refs>0);--p->refs;}
static int cpufreq_table_count_valid_entries(struct cpufreq_policy*p){int n=0;struct cpufreq_frequency_table*e;if(table_count_override>=0)return table_count_override;cpufreq_for_each_valid_entry(e,p->freq_table)++n;return n;}
static int cpuhp_setup_state(int st,const char*n,int(*fn)(unsigned),void*off){if(hp_error)return hp_error;hp_callback=fn;hp_live=1;for(unsigned i=0;i<nr_cpu_ids;++i)fn(i);return 101;}
static void cpuhp_remove_state_nocalls(int st){assert(!held_count&&!spins);assert(hp_live);hp_live=0;hp_callback=NULL;}
struct devfreq{int unused;};struct dev_pm_opp{int unused;};
static struct devfreq df;static struct dev_pm_opp opp;
static int devfreq_missing,opp_count,opp_error_at=-1,opp_calls,opp_refs;
static unsigned long opp_freqs[16];
static struct devfreq*devfreq_get_devfreq_by_node(struct device_node*n){return devfreq_missing?ERR_PTR(-ENODEV):&df;}
static int dev_pm_opp_get_opp_count(struct device*d){return opp_count;}
static struct dev_pm_opp*dev_pm_opp_find_freq_floor(struct device*d,unsigned long*f){if(opp_calls++==opp_error_at)return ERR_PTR(-EIO);bool found=false;unsigned long best=0;for(int i=0;i<opp_count;++i)if(opp_freqs[i]<=*f&&(!found||opp_freqs[i]>best)){best=opp_freqs[i];found=true;}if(!found)return ERR_PTR(-ENOENT);*f=best;++opp_refs;return &opp;}
static void dev_pm_opp_put(struct dev_pm_opp*p){assert(opp_refs>0);--opp_refs;}
struct sockaddr_qrtr{unsigned sq_family,sq_node,sq_port;};
struct qmi_elem_info{int unused;};
struct qmi_response_type_v01{uint16_t result,error;};
struct qmi_handle;struct qmi_txn;
struct qmi_service{unsigned node,port;};
struct qmi_ops{int(*new_server)(struct qmi_handle*,struct qmi_service*);void(*del_server)(struct qmi_handle*,struct qmi_service*);void(*net_reset)(struct qmi_handle*);};
struct qmi_msg_handler{int type,msg_id;struct qmi_elem_info*ei;size_t decoded_size;void(*fn)(struct qmi_handle*,struct sockaddr_qrtr*,struct qmi_txn*,const void*);};
struct qmi_handle{const struct qmi_ops*ops;struct qmi_msg_handler*handlers;bool live;};
struct qmi_txn{struct qmi_handle*handle;void*response;int id;bool active;};
static int handle_live,handle_calls,lookup_calls,fail_handle,fail_lookup,txn_error,send_error,wait_error,remote_error,txn_live,txn_sends,txn_cancels;
static void(*fill_response)(struct qmi_txn*);static void(*during_wait)(struct qmi_txn*);static void(*capture_request)(int,void*);
static struct qmi_service server={7,19};
static int qmi_handle_init(struct qmi_handle*h,size_t max,const struct qmi_ops*ops,struct qmi_msg_handler*handlers){if(++handle_calls==fail_handle)return -ENOMEM;h->ops=ops;h->handlers=handlers;h->live=true;++handle_live;return 0;}
static int qmi_add_lookup(struct qmi_handle*h,int id,int version,int instance){if(++lookup_calls==fail_lookup)return -EIO;return h->ops->new_server(h,&server);}
static void qmi_handle_release(struct qmi_handle*h){assert(h->live&&!held_count&&!spins);assert(!txn_live&&!zone_live&&!cdev_live);h->ops->del_server(h,NULL);h->live=false;--handle_live;}
static int qmi_txn_init(struct qmi_handle*h,struct qmi_txn*t,struct qmi_elem_info*ei,void*resp){assert(h->live&&!spins);if(txn_error)return txn_error;t->handle=h;t->response=resp;t->active=true;++txn_live;return 0;}
static int qmi_send_request(struct qmi_handle*h,struct sockaddr_qrtr*sq,struct qmi_txn*t,int id,size_t len,struct qmi_elem_info*ei,void*req){assert(h->live&&t->active&&!spins&&sq&&sq->sq_node==server.node&&sq->sq_port==server.port);++txn_sends;t->id=id;if(capture_request)capture_request(id,req);return send_error;}
static int qmi_txn_wait(struct qmi_txn*t,int timeout){assert(t->active&&!spins&&timeout==100);if(during_wait)during_wait(t);if(fill_response)fill_response(t);((struct qmi_response_type_v01*)t->response)->result=remote_error;t->active=false;--txn_live;return wait_error;}
static void qmi_txn_cancel(struct qmi_txn*t){assert(t->active);t->active=false;--txn_live;++txn_cancels;}
struct notifier_block{int(*notifier_call)(struct notifier_block*,unsigned long,void*);};
static int notifier_live,fail_notifier;
static int register_pm_notifier(struct notifier_block*n){if(fail_notifier)return -EIO;++notifier_live;return 0;}
static void unregister_pm_notifier(struct notifier_block*n){assert(notifier_live>0);--notifier_live;}
static void reset_faults(void){fail_action=fail_alloc=alloc_calls=fail_cdev=cdev_calls=fail_zone=zone_calls=fail_handle=handle_calls=fail_lookup=lookup_calls=0;txn_error=send_error=wait_error=remote_error=txn_sends=txn_cancels=0;qos_add_error=qos_update_error=qos_updates=0;hp_error=supplier_missing=link_failure=devfreq_missing=policy_missing=0;opp_error_at=-1;opp_calls=0;table_count_override=-1;fail_notifier=0;capture_request=NULL;during_wait=NULL;fill_response=NULL;}
static void clean(void){assert(!alloc_live&&!qos_live&&!cdev_live&&!zone_live&&!handle_live&&!txn_live&&!notifier_live&&!hp_live&&!work_pending&&!opp_refs&&!policy.refs);assert(!held_count&&!spins);}
static struct device_node node(const char*name){struct device_node n={.name=name,.refs=1};return n;}
