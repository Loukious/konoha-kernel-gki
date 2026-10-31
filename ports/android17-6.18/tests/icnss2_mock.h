#ifndef IICNSS2_TEST_MOCK_H
#define IICNSS2_TEST_MOCK_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef uint32_t __le32;typedef uint16_t __le16;typedef uint32_t __u32;typedef uint16_t __u16;typedef uint64_t dma_addr_t;typedef int gfp_t;typedef int spinlock_t;typedef int atomic_t;
#ifdef IPA_REAL_MUTEX
#include <pthread.h>
struct mutex{pthread_mutex_t lock;};
#else
struct mutex{int held;};
#endif
struct kref{int refs;};struct completion{int done;};typedef struct{int wakes;} wait_queue_head_t;
struct list_head{struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
#define INIT_LIST_HEAD(p) ((p)->next=(p),(p)->prev=(p))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define list_entry(p,t,m) container_of(p,t,m)
#define list_first_entry(p,t,m) list_entry((p)->next,t,m)
#define list_last_entry(p,t,m) list_entry((p)->prev,t,m)
#define list_for_each_entry(p,h,m) for(p=list_entry((h)->next,typeof(*p),m);&(p)->m!=(h);p=list_entry((p)->m.next,typeof(*p),m))
#define list_for_each_entry_safe(p,n,h,m) for(p=list_entry((h)->next,typeof(*p),m),n=list_entry((p)->m.next,typeof(*p),m);&(p)->m!=(h);p=n,n=list_entry((n)->m.next,typeof(*n),m))
#define list_for_each_safe(p,n,h) for(p=(h)->next,n=p->next;p!=(h);p=n,n=p->next)
static bool list_empty(struct list_head*h){return h->next==h;}
static void list_add_tail(struct list_head*n,struct list_head*h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head*n){n->prev->next=n->next;n->next->prev=n->prev;}
static void list_del_init(struct list_head*n){list_del(n);INIT_LIST_HEAD(n);}
static void list_move_tail(struct list_head*n,struct list_head*h){list_del(n);list_add_tail(n,h);}
static void list_splice_tail_init(struct list_head*a,struct list_head*h){if(list_empty(a))return;struct list_head*f=a->next,*l=a->prev;f->prev=h->prev;h->prev->next=f;l->next=h;h->prev=l;INIT_LIST_HEAD(a);}
#define list_splice_init(a,b) list_splice_tail_init(a,b)
#define spin_lock_irqsave(p,f) ((void)(p),(f)=0)
#define spin_unlock_irqrestore(p,f) ((void)(p),(void)(f))
#define spin_lock_init(p) (*(p)=0)
#define DEFINE_SPINLOCK(n) int n
#ifdef IPA_REAL_MUTEX
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define mutex_init(p) assert(!pthread_mutex_init(&(p)->lock,NULL))
static void mutex_lock(struct mutex*m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex*m){assert(!pthread_mutex_unlock(&m->lock));}
#else
#define DEFINE_MUTEX(n) struct mutex n
#define mutex_init(p) ((p)->held=0)
static void mutex_lock(struct mutex*m){assert(!m->held);m->held=1;}
static void mutex_unlock(struct mutex*m){assert(m->held);m->held=0;}
#define lockdep_assert_held(p) assert((p)->held)
#endif
#define READ_ONCE(p) (p)
#define WRITE_ONCE(p,v) ((p)=(v))
#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define ERR_PTR(x) ((void*)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>(uintptr_t)-4096)
#define IS_ERR_OR_NULL(x) (!(x)||IS_ERR(x))
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define log_event_err(...) ((void)0)
#define log_event_dbg(...) ((void)0)
#define qdss_log(...) ((void)0)
#define WARN_ON(x) assert(!(x))
#define __maybe_unused
#define __user
#define ERESTARTSYS 512
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define cpu_to_le16(x) (x)
#define le16_to_cpu(x) (x)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
static u32 get_unaligned_le32(const void*p){u32 x;memcpy(&x,p,4);return x;}
static u16 get_unaligned_le16(const void*p){u16 x;memcpy(&x,p,2);return x;}
static void put_unaligned_le32(u32 v,void*p){memcpy(p,&v,4);}
static void kref_init(struct kref*r){r->refs=1;}
static void kref_get(struct kref*r){assert(r->refs>0);r->refs++;}
static void kref_put(struct kref*r,void(*release)(struct kref*)){assert(r->refs>0);if(!--r->refs)release(r);}
static void atomic_set(atomic_t*p,int v){*p=v;}
static int atomic_read(atomic_t*p){return *p;}
static int atomic_cmpxchg(atomic_t*p,int from,int to){int old=*p;if(old==from)*p=to;return old;}
static int live,alloc_step,alloc_fault;
static bool allocation_fault(void){return ++alloc_step==alloc_fault;}
static void*kzalloc(size_t n,int f){(void)f;if(allocation_fault())return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
#define kmalloc(n,f) kzalloc(n,f)
static void kfree(const void*p){if(p){assert(live>0);live--;free((void*)p);}}
static char*mock_name(const char*n){char*p=kzalloc(strlen(n)+1,0);if(p)strcpy(p,n);return p;}
static void init_completion(struct completion*c){c->done=0;}
#define reinit_completion(c) init_completion(c)
static void complete(struct completion*c){c->done=1;}
#define complete_all(c) complete(c)
#define init_waitqueue_head(w) ((w)->wakes=0)
#define wake_up(w) ((w)->wakes++)
#define wake_up_all(w) wake_up(w)
#define wake_up_interruptible(w) wake_up(w)
struct work_struct{int pending;};struct workqueue_struct{int alive;};struct delayed_work{struct work_struct work;};struct timer_list{int stopped;};
static int queues;
static struct workqueue_struct*create_singlethread_workqueue(const char*n){(void)n;if(allocation_fault())return NULL;struct workqueue_struct*w=calloc(1,sizeof(*w));assert(w);queues++;w->alive=1;return w;}
static void destroy_workqueue(struct workqueue_struct*w){assert(w&&w->alive);queues--;free(w);}
#define INIT_WORK(w,f) ((w)->pending=0)
#define INIT_DELAYED_WORK(w,f) INIT_WORK(&(w)->work,f)
static bool queue_work(struct workqueue_struct*w,struct work_struct*s){assert(w&&w->alive);s->pending=1;return true;}
#define queue_delayed_work(w,s,t) queue_work(w,&(s)->work)
#define flush_workqueue(w) ((void)(w))
#define cancel_work_sync(w) ((w)->pending=0)
#define cancel_delayed_work_sync(w) ((w)->work.pending=0)
#define timer_shutdown_sync(t) ((t)->stopped=1)
#define timer_setup(t,f,z) ((t)->stopped=0)
static struct work_struct *running_work;
static struct work_struct*current_work(void){return running_work;}
static struct module{int state,refs;struct{void*base;size_t size;}mem[1];}self_module;
#define THIS_MODULE (&self_module)
#define MOD_TEXT 0
#define MODULE_STATE_COMING 1
#define MODULE_STATE_LIVE 2
#define MODULE_STATE_GOING 3
#define NOTIFY_DONE 0
struct notifier_block{int (*notifier_call)(struct notifier_block*,unsigned long,void*);};
static bool try_module_get(struct module*m){if(m->state==MODULE_STATE_GOING)return false;m->refs++;return true;}
static void module_put(struct module*m){if(m){assert(m->refs>0);m->refs--;}}
static bool within_module_mem_type(unsigned long a,const struct module*m,int t){return a-(unsigned long)m->mem[t].base<m->mem[t].size;}
#endif

#define __init
#define __exit
#define cnss_pr_err(...) ((void)0)
#define cnss_pr_warn(...) ((void)0)
#define cnss_pr_dbg(...) ((void)0)
#define cnss_pr_info(...) ((void)0)
#define cnss_pr_vdbg(...) ((void)0)
#define cnss_pr_dbg_buf(...) ((void)0)
#define BIT(n) (1UL<<(n))
static bool test_bit(unsigned n,const unsigned long *p) { return !!(*p&BIT(n)); }
static void set_bit(unsigned n,unsigned long *p) { *p|=BIT(n); }
static void clear_bit(unsigned n,unsigned long *p) { *p&=~BIT(n); }
#define list_first_entry_or_null(h,t,m) (list_empty(h)?NULL:list_first_entry(h,t,m))
#define EPROBE_DEFER 517

#define icnss_pr_err(...) ((void)0)
#define icnss_pr_dbg(...) ((void)0)
#define icnss_pr_info(...) ((void)0)
#define icnss_pr_soc_wake(...) ((void)0)

#define icnss_pr_err(...) ((void)0)
#define icnss_pr_dbg(...) ((void)0)
#define icnss_pr_info(...) ((void)0)
#define icnss_pr_soc_wake(...) ((void)0)
