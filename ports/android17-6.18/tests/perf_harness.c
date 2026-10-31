#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#define __init
#define __exit
#define PAGE_SIZE 4096
#define BIT(n) (1U<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define min(a,b) ((a)<(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define GFP_KERNEL 0
#define FREQ_QOS_MIN 1
#define FREQ_QOS_MAX 2
#define FREQ_QOS_MIN_DEFAULT_VALUE 0
#define FREQ_QOS_MAX_DEFAULT_VALUE INT_MAX
#define CPUHP_AP_ONLINE_DYN 1
#define pr_err(...) ((void)0)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((intptr_t)(p))
#define IS_ERR(p) ((intptr_t)(p)<0&&(intptr_t)(p)>-4096)
typedef uint32_t u32;typedef uint64_t u64;typedef long long ktime_t;typedef unsigned int uint;
typedef unsigned cpumask_t;typedef cpumask_t *cpumask_var_t;
static unsigned nr_cpu_ids=8,present=0x89,possible=0x89;
#define for_each_present_cpu(c) for(c=0;c<nr_cpu_ids;c++)if(present&BIT(c))
#define for_each_possible_cpu(c) for(c=0;c<nr_cpu_ids;c++)if(possible&BIT(c))
#define for_each_cpu(c,m) for(c=0;c<nr_cpu_ids;c++)if(*(m)&BIT(c))
#define cpu_present(c) (!!(present&BIT(c)))
#define cpu_possible(c) (!!(possible&BIT(c)))
#define cpu_online(c) cpu_present(c)
#define per_cpu(n,c) ((n)[c])
static void cpumask_clear(cpumask_t *m){*m=0;}
static void cpumask_set_cpu(unsigned c,cpumask_t*m){*m|=BIT(c);}
static bool cpumask_empty(cpumask_t*m){return !*m;}
static int cpus_locked;
static void cpus_read_lock(void){assert(!cpus_locked);cpus_locked=1;}
static void cpus_read_unlock(void){assert(cpus_locked);cpus_locked=0;}
static unsigned calls,fail_at,allocations,removes,policy_gets,policy_puts;
static int failure(void){return ++calls==fail_at;}
static void *kcalloc(size_t n,size_t s,int flags){if(failure())return NULL;void*p=calloc(n,s);assert(p);allocations++;return p;}
static void kfree(void*p){if(p){assert(allocations);allocations--;free(p);}}
static char *kmemdup_nul(const void*b,size_t n,int flags){char*p=kcalloc(n+1,1,flags);if(p)memcpy(p,b,n);return p;}
static bool alloc_cpumask_var(cpumask_var_t *p,int f){*p=kcalloc(1,sizeof(**p),f);return !!*p;}
static void free_cpumask_var(cpumask_var_t p){kfree(p);}
static int kstrtouint(const char*s,unsigned base,unsigned*v){char*e;errno=0;if(*s=='-')return -EINVAL;unsigned long n=strtoul(s,&e,base);if(errno||e==s||*e||n>UINT_MAX)return -EINVAL;*v=n;return 0;}
struct cpufreq_policy {int constraints,refs;};
struct freq_qos_request {struct cpufreq_policy *policy;int value;bool active;};
static struct cpufreq_policy policies[8];
static struct cpufreq_policy *cpufreq_cpu_get(unsigned c){if(failure())return NULL;policies[c].refs++;policy_gets++;return &policies[c];}
static void cpufreq_cpu_put(struct cpufreq_policy*p){assert(p->refs>0);p->refs--;policy_puts++;}
static int freq_qos_add_request(int*c,struct freq_qos_request*r,int type,int value){if(failure())return -71;assert(!r->active);r->policy=(void*)((char*)c - offsetof(struct cpufreq_policy,constraints));r->value=value;r->active=true;return 1;}
static bool freq_qos_request_active(struct freq_qos_request*r){return r->active;}
static void freq_qos_remove_request(struct freq_qos_request*r){assert(r->active&&r->policy->refs>0);r->active=false;removes++;}
static int freq_qos_update_request(struct freq_qos_request*r,int v){assert(r->active&&r->policy->refs>0);if(failure())return -71;r->value=v;return 1;}
static pthread_mutex_t freq_pmqos_lock=PTHREAD_MUTEX_INITIALIZER,gfx_read_lock=PTHREAD_MUTEX_INITIALIZER,core_ctl_lock=PTHREAD_MUTEX_INITIALIZER,perfevent_lock=PTHREAD_MUTEX_INITIALIZER,gfx_circ_buff_lock=PTHREAD_MUTEX_INITIALIZER;
#define mutex_lock(l) assert(!pthread_mutex_lock(l))
#define mutex_unlock(l) assert(!pthread_mutex_unlock(l))
#define spin_lock_irqsave(l,f) do{f=0;mutex_lock(l);}while(0)
#define spin_unlock_irqrestore(l,f) mutex_unlock(l)
#define spin_lock_init(l) ((void)0)
struct completion {pthread_mutex_t lock;pthread_cond_t cv;unsigned count;bool all;};
static unsigned completion_waiters;
static struct completion gfx_evt_arrival={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,false};
static void complete(struct completion*c){mutex_lock(&c->lock);c->count++;pthread_cond_broadcast(&c->cv);mutex_unlock(&c->lock);}
static void complete_all(struct completion*c){mutex_lock(&c->lock);c->all=true;pthread_cond_broadcast(&c->cv);mutex_unlock(&c->lock);}
static int wait_for_completion_interruptible(struct completion*c){mutex_lock(&c->lock);while(!c->count&&!c->all){completion_waiters++;pthread_cond_broadcast(&c->cv);pthread_cond_wait(&c->cv,&c->lock);completion_waiters--;}if(c->count)c->count--;mutex_unlock(&c->lock);return 0;}
static _Atomic long long ticks;
static ktime_t ktime_get(void){return atomic_fetch_add(&ticks,1);}
static long long ktime_to_us(ktime_t v){return v;}
static int scnprintf(char*b,size_t n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r<0?r:n?(r<(int)n?r:n-1):0;}
static int game_status_pid;
#define atomic_read(p) (*(p))
struct kobject {int ignored;};struct kobj_attribute{int ignored;};struct kset{struct kobject kobj;};struct attribute_group{int ignored;};struct work_struct{int ignored;};struct task_struct{int ignored;};
static struct kobject kernel_obj,*kernel_kobj=&kernel_obj;
static struct kset *kset_create_and_add(const char*n,void*a,struct kobject*b){return kcalloc(1,sizeof(struct kset),0);}
static void kset_unregister(struct kset*p){kfree(p);}
static struct kobject *kobject_create_and_add(const char*n,struct kobject*p){return kcalloc(1,sizeof(struct kobject),0);}
static void kobject_put(struct kobject*p){kfree(p);}
static int sysfs_create_group(struct kobject*p,struct attribute_group*g){return failure()?-71:0;}
static void sysfs_remove_group(struct kobject*p,struct attribute_group*g){assert(p);}
static struct task_struct *kthread_run(int(*fn)(void*),void*d,const char*n){void*p=kcalloc(1,sizeof(struct task_struct),0);return p?p:ERR_PTR(-ENOMEM);}
static void kthread_stop(struct task_struct*p){assert(p&&!IS_ERR(p));kfree(p);}
static int events_notify_userspace(void*d){return 0;}
static void cancel_work_sync(struct work_struct*w){}
static void core_ctl_notifier_unregister(int*p){}
static int cpuhp_setup_state_nocalls_cpuslocked(int s,const char*n,int(*a)(unsigned),int(*b)(unsigned)){assert(cpus_locked);return failure()?-71:45;}
static void cpuhp_remove_state_nocalls(int s){assert(s==45);}
static int hotplug_notify_up(unsigned c){return 0;}static int hotplug_notify_down(unsigned c){return 0;}
static unsigned long arch_scale_cpu_capacity(unsigned c){return c?1024:512;}
static u64 pmu_total[8];static int pmu_error;
static int qcom_pmu_read(unsigned c,u32 event,u64*v){if(pmu_error)return pmu_error;*v=pmu_total[c];return 0;}
/* TYPES */
static struct cpu_status msm_perf_cpu_stats[8];static struct freq_qos_request qos_req_min[8],qos_req_max[8];
static cpumask_var_t limit_mask_min,limit_mask_max;
static bool ready_for_freq_updates,perf_stopping,core_ctl_register,param_group_created,events_group_created,notify_group_created,cpu_is_hp[8];
static int perf_hp_state=-1,msm_perf_nb;
static struct gpu_data gpu_circ_buff[QUEUE_POOL_SIZE];static struct queue_indicies curr_pos;
static struct event_data **pmu_events;static unsigned long min_cpu_capacity=ULONG_MAX;
static struct {pthread_mutex_t cpu_hotplug_lock;bool init_success;} events_group;
static struct task_struct *events_notify_thread;static struct work_struct sysfs_notify_work;
static struct kset *msm_perf_kset;static struct kobject *param_kobj,*events_kobj,*notify_kobj;
static struct attribute_group param_attr_group,events_attr_group,notify_attr_group;
/* FUNCTIONS */
static void reset(void){
 assert(!allocations);memset(policies,0,sizeof(policies));memset(msm_perf_cpu_stats,0,sizeof(msm_perf_cpu_stats));memset(qos_req_min,0,sizeof(qos_req_min));memset(qos_req_max,0,sizeof(qos_req_max));memset(&curr_pos,0,sizeof(curr_pos));
 limit_mask_min=limit_mask_max=NULL;pmu_events=NULL;msm_perf_kset=NULL;param_kobj=events_kobj=notify_kobj=NULL;events_notify_thread=NULL;ready_for_freq_updates=false;core_ctl_register=false;perf_stopping=false;param_group_created=events_group_created=notify_group_created=false;perf_hp_state=-1;calls=fail_at=removes=policy_gets=policy_puts=0;gfx_evt_arrival.count=0;gfx_evt_arrival.all=false;game_status_pid=42;atomic_store(&ticks,1);
}
static void release_freq(void){for(unsigned c=0;c<nr_cpu_ids;c++){if(qos_req_min[c].active)freq_qos_remove_request(&qos_req_min[c]);if(qos_req_max[c].active)freq_qos_remove_request(&qos_req_max[c]);if(msm_perf_cpu_stats[c].policy){cpufreq_cpu_put(msm_perf_cpu_stats[c].policy);msm_perf_cpu_stats[c].policy=NULL;}}}
static void *blocked_reader(void*arg){char buf[PAGE_SIZE];assert(show_perf_gfx_evts(NULL,NULL,buf)==-ENODEV);return NULL;}
static void *producer(void*arg){unsigned id=(uintptr_t)arg;for(unsigned i=1;i<=2000;i++)msm_perf_events_update(MSM_PERF_GFX,MSM_PERF_QUEUE,42,id,id*10000+i,true);return NULL;}
int main(void){unsigned parse_cases=0,qos_cases=0,ring_cases=0,pmu_cases=0,init_cases=0;
 reset();cpumask_t mask;unsigned values[8];
 const char*invalid[]={"", " ","0", "0:",":1","0:1x","-1:2","8:1","2:5","0:1:2","0:4294967296","0:-1","0:2 3:x","0:2147483648"};
 for(unsigned i=0;i<ARRAY_SIZE(invalid);i++){assert(msm_perf_parse_freq(invalid[i],strlen(invalid[i]),&mask,values,false)==-EINVAL);parse_cases++;}
 for(unsigned c=0;c<8;c++)for(unsigned v=0;v<100;v++){char b[64];snprintf(b,sizeof(b),"%u:%u \n",c,v);int r=msm_perf_parse_freq(b,strlen(b),&mask,values,false);assert(r==(cpu_present(c)?0:-EINVAL));if(!r)assert(mask==BIT(c)&&values[c]==v);parse_cases++;}
 const char *dup="0:2 3:9 0:7 7:4294967295";assert(!msm_perf_parse_freq(dup,strlen(dup),&mask,values,true));assert(values[0]==7&&values[7]==INT_MAX);parse_cases++;
 for(unsigned f=1;f<=9;f++){reset();fail_at=f;assert(freq_qos_request_init()<0);for(unsigned c=0;c<8;c++)assert(!qos_req_min[c].active&&!qos_req_max[c].active&&!policies[c].refs);assert(policy_gets==policy_puts);qos_cases++;}
 reset();assert(alloc_cpumask_var(&limit_mask_min,0)&&alloc_cpumask_var(&limit_mask_max,0));const char*set="0:100 3:200 7:300";assert(msm_perf_set_freq(set,strlen(set),false)==strlen(set));assert(msm_perf_cpu_stats[3].min==200&&policies[3].refs==1);calls=0;fail_at=4;const char*next="0:101 3:201 7:301";assert(msm_perf_set_freq(next,strlen(next),false)==-71);assert(msm_perf_cpu_stats[0].min==101&&msm_perf_cpu_stats[3].min==200);qos_cases++;release_freq();free_cpumask_var(limit_mask_min);free_cpumask_var(limit_mask_max);assert(!allocations);
 for(unsigned f=1;f<=14;f++){reset();fail_at=f;int r=msm_performance_init();if(!r)msm_performance_cleanup();assert(!allocations);for(unsigned c=0;c<8;c++)assert(!policies[c].refs);init_cases++;}
 reset();struct event_data e={.event_id=8,.prev_count=100};pmu_total[0]=120;assert(!msm_perf_read_event(&e,0)&&e.cur_delta==20);pmu_total[0]=3;assert(!msm_perf_read_event(&e,0)&&e.cur_delta==3);pmu_error=-71;assert(msm_perf_read_event(&e,0)==-71&&e.prev_count==3);pmu_error=0;pmu_cases+=3;
 reset();for(unsigned event=0;event<6;event++)for(unsigned valid=0;valid<8;valid++){int old=curr_pos.head;msm_perf_events_update(MSM_PERF_GFX,event,(valid&1)?42:99,1,(valid&2)?1:0,valid&4);bool accepted=event>=1&&event<=3&&valid==7;assert((curr_pos.head!=old)==accepted);ring_cases++;}
 reset();for(unsigned i=1;i<=800;i++)msm_perf_events_update(MSM_PERF_GFX,MSM_PERF_QUEUE,42,i,i,true);assert((curr_pos.head-curr_pos.tail+QUEUE_POOL_SIZE)%QUEUE_POOL_SIZE==511);assert(gpu_circ_buff[curr_pos.tail].timestamp==290);ring_cases++;
 char buf[PAGE_SIZE];unsigned records=0;while(curr_pos.head!=curr_pos.tail){ssize_t n=show_perf_gfx_evts(NULL,NULL,buf);assert(n>0&&n<PAGE_SIZE);for(ssize_t i=0;i<n;i++)if(buf[i]==':')records++;}assert(records==511);ring_cases++;
 reset();pthread_t writers[4];for(uintptr_t i=0;i<4;i++)pthread_create(&writers[i],NULL,producer,(void*)i);for(unsigned i=0;i<4;i++)pthread_join(writers[i],NULL);mutex_lock(&gfx_circ_buff_lock);for(int i=curr_pos.tail;i!=curr_pos.head;i=(i+1)%QUEUE_POOL_SIZE){struct gpu_data*d=&gpu_circ_buff[i];assert(d->pid==42&&d->evt_typ==MSM_PERF_QUEUE&&d->timestamp/10000==(unsigned)d->ctx_id&&d->arrive_ts>0);}mutex_unlock(&gfx_circ_buff_lock);ring_cases++;
 reset();pthread_t blocked;pthread_create(&blocked,NULL,blocked_reader,NULL);mutex_lock(&gfx_evt_arrival.lock);while(!completion_waiters)pthread_cond_wait(&gfx_evt_arrival.cv,&gfx_evt_arrival.lock);mutex_unlock(&gfx_evt_arrival.lock);msm_performance_cleanup();pthread_join(blocked,NULL);assert(!allocations);ring_cases++;
 printf("{\"parse_cases\":%u,\"qos_failure_cases\":%u,\"init_failure_cases\":%u,\"pmu_cases\":%u,\"ring_cases\":%u,\"concurrent_producer_events\":8000}\n",parse_cases,qos_cases,init_cases,pmu_cases,ring_cases);
}
