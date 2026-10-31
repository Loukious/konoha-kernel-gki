#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#define PAGE_SHIFT 12
#define READ_ONCE(x) (x)
#define unlikely(x) (x)
#define mutex_lock(p) assert(!pthread_mutex_lock(p))
#define mutex_unlock(p) assert(!pthread_mutex_unlock(p))
typedef uint64_t u64;typedef int pid_t;typedef unsigned gfp_t;
static pthread_rwlock_t sysstats_kgsl_srcu=PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t sysstats_kgsl_lock=PTHREAD_MUTEX_INITIALIZER;
static _Atomic(u64(*)(pid_t)) sysstats_kgsl_get_stats;
static int srcu_read_lock(pthread_rwlock_t*p){assert(!pthread_rwlock_rdlock(p));return 0;}
static void srcu_read_unlock(pthread_rwlock_t*p,int i){assert(!pthread_rwlock_unlock(p));}
#define srcu_dereference(p,s) atomic_load(&(p))
#define rcu_assign_pointer(p,v) atomic_store(&(p),v)
static void sync_started(void);
static void synchronize_srcu(pthread_rwlock_t*p){sync_started();assert(!pthread_rwlock_wrlock(p));assert(!pthread_rwlock_unlock(p));}
struct files_struct {unsigned long bytes;};
struct task_struct {struct task_struct *group_leader,*next;struct files_struct *files;};
#define for_each_thread(p,t) for(t=p->group_leader;t;t=t->next)
static int rcu_depth,task_locks,max_locks;
static void rcu_read_lock(void){rcu_depth++;}
static void rcu_read_unlock(void){assert(rcu_depth>0);rcu_depth--;}
static void task_lock(struct task_struct*t){assert(rcu_depth>0);task_locks++;if(task_locks>max_locks)max_locks=task_locks;assert(task_locks==1);}
static void task_unlock(struct task_struct*t){assert(task_locks==1);task_locks--;}
static int get_dma_info(const void*d,void*f,unsigned n){return 0;}
static int iterate_fd(struct files_struct*f,unsigned n,int(*cb)(const void*,void*,unsigned),void*data){assert(task_locks==1);*(unsigned long*)data+=f->bytes;return 0;}
struct nlattr {unsigned len;unsigned char data[64];};
#define nla_len(n) ((n)->len)
#define nla_data(n) ((void*)(n)->data)
static bool fail_alloc;
static void *kmalloc(size_t n,gfp_t f){return fail_alloc?NULL:malloc(n);}
/* FUNCTIONS */
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static bool entered,release_callback,sync_waiting;
static void sync_started(void){mutex_lock(&gate);sync_waiting=true;pthread_cond_broadcast(&cv);mutex_unlock(&gate);}
static atomic_bool finished;
static u64 callback(pid_t pid){return (u64)(unsigned)pid*17;}
static u64 blocking_callback(pid_t pid){mutex_lock(&gate);entered=true;pthread_cond_broadcast(&cv);while(!release_callback)pthread_cond_wait(&cv,&gate);mutex_unlock(&gate);return 99;}
static void *reader(void*p){assert(sysstats_kgsl_stats(42)==99);return NULL;}
static void *unregisterer(void*p){sysstats_unregister_kgsl_stats_cb();atomic_store(&finished,true);return NULL;}
static void *replacer(void*p){sysstats_register_kgsl_stats_cb(callback);atomic_store(&finished,true);return NULL;}
int main(void){unsigned calls=0,fd_cases=0,strings=0;
 assert(!sysstats_kgsl_stats(1));sysstats_register_kgsl_stats_cb(callback);for(unsigned i=0;i<1000;i++){assert(sysstats_kgsl_stats(i)==i*17);calls++;}sysstats_unregister_kgsl_stats_cb();assert(!sysstats_kgsl_stats(2));
 for(unsigned replace=0;replace<2;replace++){entered=release_callback=false;atomic_store(&finished,false);sysstats_register_kgsl_stats_cb(blocking_callback);pthread_t r,u;pthread_create(&r,NULL,reader,NULL);mutex_lock(&gate);while(!entered)pthread_cond_wait(&cv,&gate);mutex_unlock(&gate);mutex_lock(&gate);sync_waiting=false;mutex_unlock(&gate);pthread_create(&u,NULL,replace?replacer:unregisterer,NULL);mutex_lock(&gate);while(!sync_waiting)pthread_cond_wait(&cv,&gate);assert(!atomic_load(&finished));release_callback=true;pthread_cond_broadcast(&cv);mutex_unlock(&gate);pthread_join(r,NULL);pthread_join(u,NULL);assert(atomic_load(&finished));assert(sysstats_kgsl_stats(2)==(replace?34:0));sysstats_unregister_kgsl_stats_cb();}
 for(unsigned mode=0;mode<8;mode++){struct files_struct shared={4096},other={8192};struct task_struct tasks[3];for(unsigned i=0;i<3;i++)tasks[i]=(struct task_struct){.group_leader=&tasks[0],.next=i<2?&tasks[i+1]:NULL,.files=(mode&(1U<<i))?&shared:&other};unsigned long bytes=tasks[0].files->bytes;for(unsigned i=1;i<3;i++)if(tasks[i].files!=tasks[0].files)bytes+=tasks[i].files->bytes;for(unsigned i=0;i<3;i++){assert(get_task_unreclaimable_info(&tasks[i])==(bytes>>PAGE_SHIFT));assert(!task_locks&&!rcu_depth);fd_cases++;}}
 for(unsigned len=0;len<64;len++)for(unsigned nul=0;nul<2;nul++){struct nlattr n={.len=len};memset(n.data,'a',sizeof(n.data));if(nul&&len)n.data[len-1]=0;char*p=nla_strdup_cust(&n,0);assert(p);assert(strlen(p)==len-(nul&&len));free(p);strings++;}fail_alloc=true;struct nlattr n={.len=1};assert(!nla_strdup_cust(&n,0));strings++;
 printf("{\"callback_cases\":%u,\"callback_drain_cases\":2,\"fd_lock_cases\":%u,\"attribute_string_cases\":%u}\n",calls,fd_cases,strings);
}
