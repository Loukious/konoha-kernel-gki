#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
typedef uint32_t uint32_t;typedef uint64_t u64;
#define __init
#define GFP_KERNEL 0
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define EXPORT_SYMBOL_GPL(x)
#define smp_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#define smp_load_acquire(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define CAT2(a,b) a##b
#define CAT(a,b) CAT2(a,b)
static pthread_mutex_t *locked(pthread_mutex_t *p) {pthread_mutex_lock(p);return p;}
static void unlocked(pthread_mutex_t **p) {pthread_mutex_unlock(*p);}
#define guard(x) pthread_mutex_t *CAT(lock_,__LINE__) __attribute__((cleanup(unlocked)))=locked
struct module {int refs;bool live;};static struct module owner={.live=true};
#define THIS_MODULE (&owner)
static bool try_module_get(struct module *m) {if(!m->live)return false;m->refs++;return true;}
static void __module_get(struct module *m) {m->refs++;}
struct qseecom_handle {int unused;};
struct qseecom_drv_ops {struct module *owner;int (*qseecom_send_command)(struct qseecom_handle*,void*,uint32_t,void*,uint32_t);int (*qseecom_start_app)(struct qseecom_handle**,char*,uint32_t);int (*qseecom_shutdown_app)(struct qseecom_handle**);int (*qseecom_process_listener_from_smcinvoke)(uint32_t*,u64*,unsigned*);};
static struct qseecom_drv_ops qseecom_fun_ops;
static pthread_mutex_t qseecom_ops_lock=PTHREAD_MUTEX_INITIALIZER;
static bool qseecom_ops_ready;
static unsigned calls;
static int start(struct qseecom_handle **h,char *a,uint32_t n){(void)h;(void)a;(void)n;__atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);return 7;}
static int stop(struct qseecom_handle **h){(void)h;return 8;}
static int send(struct qseecom_handle *h,void *s,uint32_t n,void *r,uint32_t m){(void)h;(void)s;(void)n;(void)r;(void)m;return 9;}
static int listener(uint32_t *a,u64 *b,unsigned *c){(void)a;(void)b;(void)c;return 10;}
static int si_object_invoke_ctxs_ida;
struct si_arg {int unused;};
struct list_head {int init;};
struct si_object_invoke_ctx {unsigned context_id;struct list_head objects_head;};
#define INIT_LIST_HEAD(p) ((p)->init=1)
static int id_error,shm_error,id_live,shm_calls;
static int ida_alloc_min(int *a,int min,int flag){(void)a;(void)flag;if(id_error)return id_error;id_live++;return min;}
static void ida_free(int *a,unsigned id){(void)a;assert(id==10&&id_live);id_live--;}
static int shmem_alloc(struct si_object_invoke_ctx *c,struct si_arg *a){(void)c;(void)a;shm_calls++;return shm_error;}
struct kobject {int unused;};static struct kobject kobj,*kernel_kobj=&kobj,*si_core_kobj;
static int attr_group;
static int fault_step,steps,wq_live,obj_live,group_live,adci_live;
static bool fault(void){return ++steps==fault_step;}
static int init_si_core_wq(void){if(fault())return -ENOMEM;wq_live++;return 0;}
static void destroy_si_core_wq(void){assert(wq_live);wq_live--;}
static struct kobject *kobject_create_and_add(const char *name,struct kobject *parent){(void)name;(void)parent;if(fault())return NULL;obj_live++;return &kobj;}
static void kobject_put(struct kobject *p){assert(p&&obj_live);obj_live--;}
static int sysfs_create_group(struct kobject *p,int *g){(void)p;(void)g;if(fault())return -EIO;group_live++;return 0;}
static void sysfs_remove_group(struct kobject *p,int *g){(void)p;(void)g;assert(group_live);group_live--;}
static int adci_start(void){if(fault())return -ENOMEM;adci_live++;return 0;}
/* FUNCTIONS */
static struct qseecom_drv_ops ops;
static void *publish(void *unused){(void)unused;return (void*)(intptr_t)provide_qseecom_kernel_fun_ops(&ops);}
static void *reader(void *unused){(void)unused;for(unsigned i=0;i<10000;i++)assert(qseecom_start_app(NULL,"app",16)==7);return NULL;}
int main(void) {
 assert(qseecom_start_app(NULL,"app",16)==-EAGAIN);assert(qseecom_send_command(NULL,NULL,0,NULL,0)==-EAGAIN);
 assert(provide_qseecom_kernel_fun_ops(NULL)==-EINVAL);assert(provide_qseecom_kernel_fun_ops(&ops)==-EINVAL);
 ops=(struct qseecom_drv_ops){&owner,send,start,stop,listener};owner.live=false;assert(provide_qseecom_kernel_fun_ops(&ops)==-ENODEV&&!owner.refs);owner.live=true;
 pthread_t a,b;assert(!pthread_create(&a,NULL,publish,NULL)&&!pthread_create(&b,NULL,publish,NULL));void *ra,*rb;pthread_join(a,&ra);pthread_join(b,&rb);assert((!ra&&(intptr_t)rb==-EBUSY)||(!rb&&(intptr_t)ra==-EBUSY));assert(owner.refs==1);
 assert(qseecom_shutdown_app(NULL)==8&&qseecom_send_command(NULL,NULL,0,NULL,0)==9&&qseecom_process_listener_from_smcinvoke(NULL,NULL,NULL)==10);
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,reader,NULL));for(unsigned i=0;i<4;i++)pthread_join(threads[i],NULL);assert(calls==40000);
 struct si_object_invoke_ctx ctx;struct si_arg arg;
 id_error=-ENOMEM;assert(si_object_invoke_ctx_init(&ctx,&arg)==-ENOMEM&&!id_live&&!shm_calls);id_error=-ENOSPC;assert(si_object_invoke_ctx_init(&ctx,&arg)==-ENOSPC&&!id_live&&!shm_calls);
 id_error=0;shm_error=-ENOMEM;assert(si_object_invoke_ctx_init(&ctx,&arg)==-ENOMEM&&!id_live&&shm_calls==1);shm_error=0;assert(!si_object_invoke_ctx_init(&ctx,&arg)&&ctx.context_id==10&&ctx.objects_head.init);ida_free(&si_object_invoke_ctxs_ida,ctx.context_id);
 for(int f=1;f<=4;f++){fault_step=f;steps=0;assert(si_core_init()<0);assert(!wq_live&&!obj_live&&!group_live&&!adci_live&&owner.refs==1);}
 fault_step=0;steps=0;assert(!si_core_init()&&wq_live==1&&obj_live==1&&group_live==1&&adci_live==1&&owner.refs==2);
 printf("{\"concurrent_provider_calls\":%u,\"provider_publication_race\":true,\"context_id_fault_cases\":3,\"initialization_fault_cases\":4,\"resident_after_success\":true}\n",calls);
}
