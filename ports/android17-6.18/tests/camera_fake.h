#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
typedef uint8_t u8;typedef uint32_t u32;typedef unsigned long long u64;typedef unsigned int __poll_t;
#define __user
#define __init
#define __exit
#define __rcu
#define THIS_MODULE NULL
#define GFP_KERNEL 0
#define KERN_INFO ""
#define KERN_ERR ""
#define KBUILD_MODNAME "camera_test"
#define pr_debug(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define printk(...) ((void)0)
#define module_init(x)
#define module_exit(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define EXPORT_SYMBOL(x)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define BIT(n) (1U<<(n))
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELAXED)
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
#define swap(a,b) do{__typeof__(a) tmp=(a);(a)=(b);(b)=tmp;}while(0)
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define MKDEV(a,b) (((a)<<8)|(b))
struct mutex {pthread_mutex_t raw;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->raw,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->raw));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->raw));}
typedef struct {unsigned wakeups;} wait_queue_head_t;
#define DECLARE_WAIT_QUEUE_HEAD(n) wait_queue_head_t n
static void init_waitqueue_head(wait_queue_head_t *w){w->wakeups=0;}
static void wake_up_interruptible(wait_queue_head_t *w){w->wakeups++;}
struct inode {int unused;};struct file {int id;};struct poll_table_struct {int waits;};
#define EPOLLIN 1
#define EPOLLRDNORM 2
#define EPOLLERR 4
#define EPOLLHUP 8
#define POLLIN EPOLLIN
#define POLLRDNORM EPOLLRDNORM
static void poll_wait(struct file *f,wait_queue_head_t *w,struct poll_table_struct *p){if(p)p->waits++;}
struct file_operations {void *owner;int (*open)(struct inode *,struct file *);int (*release)(struct inode *,struct file *);long (*unlocked_ioctl)(struct file *,unsigned int,unsigned long);__poll_t (*poll)(struct file *,struct poll_table_struct *);ssize_t (*read)(struct file *,char *,size_t,loff_t *);ssize_t (*write)(struct file *,const char *,size_t,loff_t *);};
static int fail_step,steps,alloc_live,chrdev_live,class_live,device_live;
static bool fail_copy,fail_alloc;
static int failure(void){return ++steps==fail_step;}
static void *kzalloc(size_t n,int flags){if(fail_alloc||failure())return NULL;void *p=calloc(1,n);assert(p);alloc_live++;return p;}
static void *kmalloc(size_t n,int flags){void *p=kzalloc(n,flags);if(p)memset(p,0xaa,n);return p;}
static void kfree(void *p){if(p){alloc_live--;free(p);}}
static unsigned long copy_from_user(void *dst,const void *src,size_t n){if(fail_copy)return n?n:1;memcpy(dst,src,n);return 0;}
static unsigned long copy_to_user(void *dst,const void *src,size_t n){if(fail_copy)return n?n:1;memcpy(dst,src,n);return 0;}
static ssize_t strscpy(void *dst,const void *src,size_t n){size_t len=strlen(src);if(!n)return -E2BIG;size_t copy=len<n?len:n-1;memcpy(dst,src,copy);((char *)dst)[copy]=0;return len<n?(ssize_t)len:-E2BIG;}
static int kstrtouint(const char *s,int base,unsigned *out){if(!s||!*s||*s=='-')return -EINVAL;char *end;errno=0;unsigned long n=strtoul(s,&end,base);if(errno||end==s||(*end&&*end!='\n')||n>UINT_MAX)return -EINVAL;*out=n;return 0;}
static int kstrtoint(const char *s,int base,int *out){unsigned v;int rc=kstrtouint(s,base,&v);if(rc||v>INT_MAX)return -EINVAL;*out=v;return 0;}
struct class {int x;};struct device {int x;};static struct class fake_class;static struct device fake_device;
static const struct file_operations *registered_fops;
static int register_chrdev(unsigned n,const char *name,const struct file_operations *f){if(failure())return -EIO;chrdev_live++;registered_fops=f;struct file file={0};assert(f->open(NULL,&file)==-ENODEV);return 42;}
static void unregister_chrdev(unsigned n,const char *name){assert(chrdev_live==1&&n==42);chrdev_live--;}
static struct class *class_create(const char *name){if(failure())return ERR_PTR(-ENOMEM);class_live++;return &fake_class;}
static void class_destroy(struct class *p){assert(p==&fake_class&&class_live==1);class_live--;}
static struct device *device_create(struct class *c,void *p,dev_t n,void *d,const char *fmt,...){if(failure())return ERR_PTR(-ENOSPC);device_live++;return &fake_device;}
static void device_destroy(struct class *c,dev_t n){assert(device_live==1);device_live--;}
struct kfifo {char *data;size_t cap,head,len;};
static int kfifo_alloc(struct kfifo *q,size_t n,int flags){size_t cap=1;while(cap<n)cap*=2;q->data=kzalloc(cap,flags);if(!q->data)return -ENOMEM;q->cap=cap;q->head=q->len=0;return 0;}
static void kfifo_free(struct kfifo *q){kfree(q->data);memset(q,0,sizeof(*q));}
static unsigned kfifo_len(struct kfifo *q){return q->len;}
static unsigned kfifo_avail(struct kfifo *q){return q->cap-q->len;}
static unsigned kfifo_in(struct kfifo *q,const void *src,size_t n){if(n>q->cap-q->len)n=q->cap-q->len;for(size_t i=0;i<n;i++)q->data[(q->head+q->len+i)%q->cap]=((char *)src)[i];q->len+=n;return n;}
static unsigned kfifo_out_peek(struct kfifo *q,void *dst,size_t n){if(n>q->len)n=q->len;for(size_t i=0;i<n;i++)((char *)dst)[i]=q->data[(q->head+i)%q->cap];return n;}
static unsigned kfifo_out(struct kfifo *q,void *dst,size_t n){n=kfifo_out_peek(q,dst,n);q->head=(q->head+n)%q->cap;q->len-=n;return n;}
struct rcu_head {int unused;};static pthread_rwlock_t rcu_lock=PTHREAD_RWLOCK_INITIALIZER;
static void rcu_read_lock(void){assert(!pthread_rwlock_rdlock(&rcu_lock));}
static void rcu_read_unlock(void){assert(!pthread_rwlock_unlock(&rcu_lock));}
static void synchronize_rcu(void){assert(!pthread_rwlock_wrlock(&rcu_lock));assert(!pthread_rwlock_unlock(&rcu_lock));}
#define rcu_dereference(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
#define RCU_INIT_POINTER(p,v) rcu_assign_pointer(p,v)
#define rcu_replace_pointer(p,v,cond) __atomic_exchange_n(&(p),v,__ATOMIC_ACQ_REL)
typedef struct {u64 bits;} cpumask_t;
static cpumask_t active={.bits=255};static const cpumask_t *cpu_active_mask=&active;static unsigned nr_cpu_ids=8;
static void cpumask_set_cpu(int c,cpumask_t *m){assert(c>=0&&c<64);m->bits|=1ULL<<c;}
static bool cpumask_test_cpu(int c,const cpumask_t *m){assert(c>=0&&c<64);return (m->bits>>c)&1;}
static bool cpu_possible(int c){return c>=0&&(unsigned)c<nr_cpu_ids;}
static bool cpu_active(int c){return cpu_possible(c)&&cpumask_test_cpu(c,&active);}
static void cpumask_andnot(cpumask_t *o,const cpumask_t *a,const cpumask_t *b){o->bits=a->bits&~b->bits;}
static void cpumask_and(cpumask_t *o,const cpumask_t *a,const cpumask_t *b){o->bits=a->bits&b->bits;}
static bool cpumask_empty(const cpumask_t *m){return !m->bits;}
static int cpumask_first(const cpumask_t *m){return __builtin_ctzll(m->bits);}
static unsigned fake_cpu;static unsigned raw_smp_processor_id(void){return fake_cpu;}
#define TASK_COMM_LEN 16
#define TASK_DEAD 1
#define EXIT_DEAD 2
#define EXIT_ZOMBIE 4
struct task_struct {int pid,tgid,refs;unsigned uid;char comm[16];void *mm,*files;unsigned nr_cpus_allowed;cpumask_t *cpus_ptr;int __state,exit_state;const char *cmdline;struct task_struct *next;};
static cpumask_t task_cpus={.bits=255};static struct task_struct caller={.pid=10,.tgid=10,.uid=1000,.mm=(void *)1,.files=(void *)1,.nr_cpus_allowed=8,.cpus_ptr=&task_cpus};
#define current (&caller)
static int init_user_ns;
static unsigned current_uid(void){return caller.uid;}
static unsigned from_kuid(void *ns,unsigned uid){return uid;}
static int task_pid_nr(struct task_struct *t){return t->pid;}
static bool in_interrupt(void){return false;}
#define PIDTYPE_PID 0
static struct task_struct *processes;
static int find_vpid(int pid){return pid;}
static struct task_struct *get_pid_task(int pid,int type){for(struct task_struct *p=processes;p;p=p->next)if(p->pid==pid){p->refs++;return p;}return NULL;}
static void get_task_struct(struct task_struct *p){p->refs++;}
static void put_task_struct(struct task_struct *p){assert(p->refs>0);p->refs--;}
static bool pid_alive(struct task_struct *p){return !p->exit_state;}
#define for_each_process(p) for(p=processes;p;p=p->next)
static void get_task_comm(char *s,struct task_struct *p){memcpy(s,p->comm,TASK_COMM_LEN);}
static char *kstrdup_quotable_cmdline(struct task_struct *p,int flags){if(!p->cmdline)return NULL;char *s=kzalloc(strlen(p->cmdline)+1,flags);if(s)strcpy(s,p->cmdline);return s;}
static u64 ktime_get(void){return 123456;}
static u64 ktime_to_ns(u64 n){return n;}
struct rq {int unused;};struct rq_flags {int unused;};
static int hook_fail,hook_calls[4];static unsigned hooks;
static int hook_register(int n){hook_calls[n]++;if(hook_fail==n+1)return -ENOMEM;assert(!(hooks&BIT(n)));hooks|=BIT(n);return 0;}
#define register_trace_android_rvh_is_cpu_allowed(fn,arg) hook_register(0)
#define register_trace_android_rvh_can_migrate_task(fn,arg) hook_register(1)
#define register_trace_android_rvh_select_task_rq_fair(fn,arg) hook_register(2)
#define register_trace_android_rvh_sched_newidle_balance(fn,arg) hook_register(3)

static bool sysfs_streq(const char *a,const char *b){size_t na=strlen(a),nb=strlen(b);if(na&&a[na-1]=='\n')na--;if(nb&&b[nb-1]=='\n')nb--;return na==nb&&!memcmp(a,b,na);}
