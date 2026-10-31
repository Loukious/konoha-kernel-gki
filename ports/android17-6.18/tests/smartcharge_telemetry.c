#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int64_t time64_t;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define CONFIG_FACTORY_BUILD 0
#define IS_ENABLED(v) (v)
#define EXPORT_SYMBOL_GPL(n)
/* TELEMETRY_HEADER */
struct mca_event_notify_data {const char *event;int event_len;};
struct misight_mievent {int unused;};
static pthread_mutex_t mievent_lock=PTHREAD_MUTEX_INITIALIZER;
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
struct mutex_guard {pthread_mutex_t *lock;};
static struct mutex_guard mutex_guard_init(pthread_mutex_t *l){pthread_mutex_lock(l);return (struct mutex_guard){l};}
static void mutex_guard_cleanup(struct mutex_guard *g){pthread_mutex_unlock(g->lock);}
#define guard(t) struct mutex_guard g __attribute__((cleanup(mutex_guard_cleanup)))=mutex_guard_init
static time64_t now;
static int allocs,writes,notifications,fail_stage,adds;
static time64_t ktime_get_boottime_seconds(void){return now;}
static struct misight_mievent *cdev_tevent_alloc(int code){(void)code;if(fail_stage==1)return NULL;allocs++;return calloc(1,sizeof(struct misight_mievent));}
static int cdev_tevent_add_str(struct misight_mievent *e,const char *k,const char *v){assert(e&&k&&v);adds++;return fail_stage==2?-ENOMEM:0;}
static int cdev_tevent_add_int(struct misight_mievent *e,const char *k,int v){(void)v;return cdev_tevent_add_str(e,k,"v");}
static int cdev_tevent_write(struct misight_mievent *e){assert(e);if(fail_stage==3)return -EIO;writes++;return 0;}
static void cdev_tevent_destroy(struct misight_mievent *e){allocs--;free(e);}
static void mca_event_report_uevent(struct mca_event_notify_data *n){assert(n->event_len==(int)strlen(n->event));__atomic_fetch_add(&notifications,1,__ATOMIC_RELAXED);}
/* STAGED_DEFINITIONS */
static void *report_thread(void *p){mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,p,1);return NULL;}
int main(void){
 int vals[CHARGE_MIEVENT_MAX_PARA_NUM]={0};char *strings[CHARGE_MIEVENT_MAX_PARA_NUM];for(int i=0;i<CHARGE_MIEVENT_MAX_PARA_NUM;i++)strings[i]="valid";
 for(int i=0;i<CHARGE_DFX_MAX_NUM;i++)assert(mievent_valid(&g_charge_mievent_info[i]));
 for(int i=-128;i<128;i++){mca_charge_mievent_report(-1,vals,i);mca_charge_mievent_report(CHARGE_DFX_MAX_NUM,vals,i);}
 for(int stage=1;stage<=3;stage++){fail_stage=stage;mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,vals,1);assert(!g_upload_type_plug_info[PLUG_TYPE_PD_AUTH_FAILED].count&&!allocs&&!writes);}fail_stage=0;
 pthread_t threads[32];for(int i=0;i<32;i++)pthread_create(&threads[i],NULL,report_thread,vals);for(int i=0;i<32;i++)pthread_join(threads[i],NULL);assert(writes==1&&notifications==1&&g_upload_type_plug_info[0].count==1);
 g_upload_type_plug_info[0].count=INT_MAX;for(int i=0;i<10000;i++)mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,vals,1);assert(g_upload_type_plug_info[0].count==INT_MAX&&writes==1);
 mca_charge_mievent_set_state(MIEVENT_STATE_PLUG,1);assert(g_upload_type_plug_info[0].count==INT_MAX);mca_charge_mievent_set_state(MIEVENT_STATE_PLUG,0);assert(!g_upload_type_plug_info[0].count);
 for(int i=0;i<CHARGE_DFX_MAX_NUM;i++){const struct charge_mievent_info *e=&g_charge_mievent_info[i];mca_charge_mievent_set_state(MIEVENT_STATE_END,i);int prev=writes;mca_charge_mievent_report(i,e->data_type==MIEVENT_DATA_TYPE_STRING?(void *)strings:(void *)vals,e->data_count);assert(writes==prev+1&&!allocs);}
 int str_idx=-1;for(int i=0;i<CHARGE_DFX_MAX_NUM;i++)if(g_charge_mievent_info[i].data_type==MIEVENT_DATA_TYPE_STRING){str_idx=i;break;}assert(str_idx>=0);mca_charge_mievent_set_state(MIEVENT_STATE_END,str_idx);int prev=writes;strings[0]=NULL;mca_charge_mievent_report(str_idx,strings,g_charge_mievent_info[str_idx].data_count);assert(writes==prev);
 char over[MIEVENT_STRING_MAX_LEN];memset(over,'X',sizeof(over));strings[0]=over;mca_charge_mievent_report(str_idx,strings,g_charge_mievent_info[str_idx].data_count);assert(writes==prev);over[sizeof(over)-1]=0;mca_charge_mievent_report(str_idx,strings,g_charge_mievent_info[str_idx].data_count);assert(writes==prev+1);
 mca_charge_mievent_set_state(MIEVENT_STATE_END,CHARGE_DFX_BATTERY_CYCLECOUNT);prev=writes;mca_charge_mievent_report(CHARGE_DFX_BATTERY_CYCLECOUNT,vals,1);mca_charge_mievent_report(CHARGE_DFX_BATTERY_CYCLECOUNT,vals,1);assert(writes==prev+1);now=MINOR_TIMER_S;mca_charge_mievent_report(CHARGE_DFX_BATTERY_CYCLECOUNT,vals,1);assert(writes==prev+2&&!allocs);
 puts("{\"passed\":true,\"concurrent_reports\":32,\"allocation_add_write_faults\":3,\"all_event_metadata_validated\":true,\"bounded_strings\":true,\"counter_saturation\":true}");
}
