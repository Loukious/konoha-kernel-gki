#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define GFP_ATOMIC 0
#define MAX_IPC_INSTANCES 9
#define MAX_DEV_NAME_SIZE 41
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>(uintptr_t)-4096)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define pr_err(...) ((void)0)
#define __init
#define __exit
#define spin_lock_irqsave(p,f) ((void)(p),(f)=0)
#define spin_unlock_irqrestore(p,f) ((void)(p),(void)(f))
#define INIT_WORK(w,fn) ((w)->func=(fn))
#define kfree free
struct work_struct {void (*func)(struct work_struct *);};
struct workqueue_struct {int unused;};
struct ipc_log_work {struct work_struct ctxt_work;char dev_name[MAX_DEV_NAME_SIZE];};
struct ipc_log {void *context;char dev_name[MAX_DEV_NAME_SIZE];};
struct ffs_data {const char *dev_name;};
struct kretprobe {struct {const char *symbol_name;} kp;};
static struct workqueue_struct *ipc_wq;
static struct ipc_log ipc_log_s[MAX_IPC_INSTANCES];
static struct work_struct *worklist[200];
static struct kretprobe ffsprobes[26];
static int num_devices,ipc_lock,work_count,contexts,registered,registration_step,registration_failure,alloc_failure;
static char names[26][41];
static size_t strscpy(char *dst,const char *src,size_t n) {size_t s=strlen(src);size_t copy=s<n?s:n-1;memcpy(dst,src,copy);dst[copy]=0;return copy;}
static size_t strlcat(char *dst,const char *src,size_t n) {size_t d=strlen(dst);return d+strscpy(dst+d,src,n-d);}
static void *kzalloc(size_t n,int flags) {(void)flags;return alloc_failure?NULL:calloc(1,n);}
static void *ipc_log_context_create(int pages,const char *name,int flags) {(void)pages;(void)name;(void)flags;contexts++;return malloc(1);}
static void ipc_log_context_destroy(void *p) {assert(p&&contexts>0&&!registered&&!work_count);contexts--;free(p);}
static bool queue_work(struct workqueue_struct *q,struct work_struct *w) {assert(q&&work_count<200);worklist[work_count++]=w;return true;}
static void flush(void) {while(work_count){struct work_struct *w=worklist[0];memmove(worklist,worklist+1,--work_count*sizeof(*worklist));w->func(w);}}
static struct workqueue_struct *alloc_ordered_workqueue(const char *name,int flags) {(void)name;(void)flags;return alloc_failure?NULL:calloc(1,sizeof(struct workqueue_struct));}
static void destroy_workqueue(struct workqueue_struct *q) {assert(!registered);flush();free(q);}
static void create_ipc_context(const char *);
static int register_kretprobe(struct kretprobe *p) {(void)p;if(++registration_step==registration_failure)return -EIO;registered++;create_ipc_context(names[registered-1]);return 0;}
static void unregister_kretprobe(struct kretprobe *p) {(void)p;assert(registered>0);registered--;}
/* FUNCTIONS */
int main(void) {
 for(int i=0;i<26;i++){snprintf(names[i],sizeof(names[i]),"function%d",i);ffsprobes[i].kp.symbol_name=names[i];}
 for(int fault=1;fault<=26;fault++){registration_step=0;registration_failure=fault;num_devices=0;memset(ipc_log_s,0,sizeof(ipc_log_s));assert(kretprobe_init()==-EIO);assert(!contexts&&!registered&&!work_count);}
 registration_failure=0;registration_step=0;num_devices=0;memset(ipc_log_s,0,sizeof(ipc_log_s));assert(!kretprobe_init());assert(registered==26&&work_count==26);flush();assert(contexts==9&&num_devices==9);struct ffs_data ffs={.dev_name=names[0]};assert(get_ipc_context(&ffs));assert(!get_ipc_context(NULL));for(int i=0;i<40;i++)create_ipc_context(names[0]);flush();assert(contexts==9&&num_devices==9);ffs.dev_name="absent";assert(!get_ipc_context(&ffs));alloc_failure=1;create_ipc_context("failed");assert(!work_count);alloc_failure=0;kretprobe_exit();assert(!contexts&&!registered&&!work_count);alloc_failure=1;assert(kretprobe_init()==-ENOMEM);
 printf("{\"registration_rollback_cases\":26,\"context_creation_and_capacity_cases\":66,\"probe_work_context_drain_order\":true}\n");
}
