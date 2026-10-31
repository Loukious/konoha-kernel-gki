#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#define CVP_FASTRPC_DRIVER_NAME_SIZE 16
#define MAX_FASTRPC_DRIVER_NUM 8
#define CPU2DSP_MAX_CMD 8
#define DRIVER_NAME_AVAILABLE 0
#define DSP_INVALID 0
#define DSP_UNINIT 1
#define CVP_INVALID_RPMSG_TYPE 0xBADDFACE
#define CVP_DBG_DUMP 0
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define dprintk(...) ((void)0)
struct mutex {int initialized;};struct list {struct mutex lock;};
static int locks,registered,running,joined,sessions,fail;
static void mutex_init(struct mutex *m){assert(!m->initialized);m->initialized=1;locks++;}
static void mutex_destroy(struct mutex *m){assert(m->initialized&&!registered&&!running&&!sessions);m->initialized=0;locks--;}
static void mutex_lock(struct mutex *m){assert(m->initialized);}
static void mutex_unlock(struct mutex *m){assert(m->initialized);}
#define INIT_MSM_CVP_LIST(p) mutex_init(&(p)->lock)
#define DEINIT_MSM_CVP_LIST(p) mutex_destroy(&(p)->lock)
static void init_completion(int *c){*c=0;}
static void complete_all(int *c){*c=1;}
struct driver_name {int status;char name[16];};
struct cvp_dsp_apps {struct mutex tx_lock,rx_lock,driver_name_lock;int state;bool hyp_assigned;int completions[9];struct {unsigned type;}pending_dsp2cpu_cmd,pending_dsp2cpu_rsp;struct list fastrpc_driver_list;struct driver_name cvp_fastrpc_name[8];void *dsp_thread;};static struct cvp_dsp_apps gfa_cv;
static int cvp_dsp_rpmsg_client,cvp_fastrpc_cleanup_work;
static void cancel_work_sync(int *w){assert(!running&&!registered&&!sessions);}
static void add_va_node_to_list(int tag,void *p,size_t n,const char *name,bool copy){}
static int cvp_register_rpmsg_driver(int *d){assert(locks==4);if(fail==1)return -EIO;registered++;return 0;}
static void cvp_unregister_rpmsg_driver(int *d){assert(registered==1&&locks==4);registered--;sessions=0;}
static int cvp_dsp_thread(void *p){return 0;}
static void *kthread_run(int(*fn)(void*),void *data,const char *name){if(fail==2)return ERR_PTR(-ENOMEM);assert(registered==1);running++;return (void*)1;}
static void kthread_stop(void *t){assert(!registered&&running==1&&gfa_cv.state==DSP_INVALID);for(int i=0;i<9;i++)assert(gfa_cv.completions[i]);running--;joined++;}
static void cvp_remove_dsp_sessions(void){assert(!running&&!registered&&locks==4);sessions=0;}
static void cvp_dsp_device_deinit(void);
#include "dsp.inc"
int main(void){for(int f=1;f<=2;f++){memset(&gfa_cv,0,sizeof(gfa_cv));fail=f;assert(cvp_dsp_device_init()<0);assert(!registered&&!locks&&!running);}for(int i=0;i<50;i++){fail=0;assert(!cvp_dsp_device_init());sessions=2;cvp_dsp_device_exit();assert(!registered&&!running&&!sessions&&locks==4);cvp_dsp_device_deinit();assert(!locks);}assert(joined==50);printf("{\"dsp_setup_failures\":2,\"worker_join_and_callback_drain\":50}\n");}
