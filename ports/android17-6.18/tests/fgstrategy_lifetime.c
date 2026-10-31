#define GFP_KERNEL 0
#define WQ_MEM_RECLAIM 0
#define STRATEGY_FG_UPDATE_PERIOD_NONE -1
#define STRATEGY_FG_ABNORMAL_DFX_DELAY 17500
#define NVT1000_OTA_MONITOR_INTERVAL 1000
#define MCA_EVENT_TYPE_PANEL 0
#define MCA_EVENT_TYPE_THERMAL_TEMP 1
#define STRATEGY_FUNC_TYPE_FG 0
static int stage,fail,resources[11],disable_count,queued,callback_calls;
static struct power_supply supply;
static struct workqueue_struct queue;
static struct strategy_fg_class_ops{int unused;}g_strategy_fg_ops;
#define mutex_init(p) pthread_mutex_init(&(p)->lock,NULL)
#define INIT_DELAYED_WORK(w,f) ((w)->initialized=1)
static int msecs_to_jiffies(int n){return n;}
static int acquire(int n){assert(++stage==n);if(fail==n)return -EIO;resources[n]++;return 0;}
static void drop(int n){assert(resources[n]==1);resources[n]--;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)f;if(acquire(1))return NULL;d->data=calloc(1,n);return d->data;}
static void platform_set_drvdata(struct platform_device *p,void *d){p->dev.data=d;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
static int dev_err_probe(struct device *d,int error,const char *s){(void)d;(void)s;return error;}
static int strategy_fg_parse_dt(struct strategy_fg *f){assert(f->last_ui_soc==-1&&f->sample_error==-EAGAIN&&!f->ready);assert(f->monitor_work.initialized&&f->delay_reset_full_flag_work.initialized&&f->reset_default_work.initialized&&f->batt_abnormal_dfx_work.initialized&&f->dtpt_monitor_work.initialized&&f->fl4p0_calibration_work.initialized&&f->force_report_full_work.initialized&&f->ota_update_work.initialized);return acquire(2);}
static struct power_supply *power_supply_get_by_name(const char *n){assert(!strcmp(n,"battery"));return acquire(3)?NULL:&supply;}
static void power_supply_put(struct power_supply *p){assert(p==&supply);drop(3);}
static struct workqueue_struct *alloc_ordered_workqueue(const char *n,int flags){(void)flags;assert(!strcmp(n,"mca_fg"));if(acquire(4))return NULL;queue.live=1;return &queue;}
static void destroy_workqueue(struct workqueue_struct *w){assert(w==&queue&&w->live&&disable_count==8);w->live=0;drop(4);}
static void disable_delayed_work_sync(struct delayed_work *w){assert(w->initialized&&!w->disabled);w->disabled=1;w->pending=0;disable_count++;}
static int queue_delayed_work(struct workqueue_struct *w,struct delayed_work *d,int time){(void)time;assert(w->live&&d->initialized&&!d->disabled);d->pending=1;queued++;return 0;}
static int charger_partition_get_eu_model(bool *v){*v=true;return 0;}
static void strategy_fg_init_voter(struct strategy_fg *f){f->voter_ok=true;}
static int strategy_fg_panel_notifier_cb(struct notifier_block *n,unsigned long e,void *v){(void)n;(void)e;(void)v;return 0;}
#define strategy_fg_thermal_notifier_cb strategy_fg_panel_notifier_cb
#define mca_strategy_fg_shutdown_cb strategy_fg_panel_notifier_cb
static int mca_event_block_notify_register(int n,struct notifier_block *b){assert(b->notifier_call);return acquire(n?6:5);}
static void mca_event_block_notify_unregister(int n,struct notifier_block *b){(void)b;drop(n?6:5);}
static int register_reboot_notifier(struct notifier_block *b){assert(b->notifier_call);return acquire(7);}
static void unregister_reboot_notifier(struct notifier_block *b){(void)b;drop(7);}
static int strategy_fg_process_event(int a,int b,void *p){(void)a;(void)b;struct strategy_fg *f=p;callback_calls++;assert(!f->ready);return -EAGAIN;}
static int strategy_fg_get_status(int a,void *v,void *p){(void)a;(void)v;(void)p;return -EAGAIN;}
static int mca_strategy_ops_register(int n,int (*cb)(int,int,void*),int (*get)(int,void*,void*),void *x,void *p){(void)n;(void)get;(void)x;assert(cb(0,0,p)==-EAGAIN);return acquire(8);}
static void mca_strategy_ops_unregister(int n,void *p){(void)n;(void)p;drop(8);}
static int strategy_class_fg_ops_register(void *p,struct strategy_fg_class_ops *o){(void)o;struct strategy_fg *f=p;assert(!f->ready);return acquire(9);}
static void strategy_class_fg_ops_unregister(void *p){(void)p;drop(9);}
static int strategy_fg_auth_create_group(struct device *d){struct strategy_fg *f=d->data;assert(!f->ready);return acquire(10);}
static void strategy_fg_auth_remove_group(struct device *d){(void)d;drop(10);}
/* STAGED_DEFINITIONS */
int main(void){
 for(int n=1;n<=10;n++){stage=0;fail=n;disable_count=queued=0;struct platform_device p={0};int ret=strategy_fg_probe(&p);assert(ret==(n==1||n==4?-ENOMEM:n==3?-EPROBE_DEFER:-EIO));for(int j=3;j<=10;j++)assert(!resources[j]);if(p.dev.data){struct strategy_fg *f=p.dev.data;assert(!f->ready&&!queued);pthread_mutex_destroy(&f->sample_lock.lock);free(f);drop(1);}if(resources[2])drop(2);assert(!queue.live);}
 stage=0;fail=0;disable_count=queued=0;struct platform_device p={0};assert(!strategy_fg_probe(&p));struct strategy_fg *f=p.dev.data;assert(f->ready&&f->is_eu_model&&f->boot_update_soc&&queued==2);strategy_fg_remove(&p);assert(f->stopping&&!f->ready&&disable_count==8);for(int j=3;j<=10;j++)assert(!resources[j]);pthread_mutex_destroy(&f->sample_lock.lock);free(f);drop(1);drop(2);
 puts("{\"probe_failure_cases\":10,\"early_callbacks_guarded\":true,\"eight_works_disabled\":true,\"ordered_queue_and_supply_released\":true,\"provider_and_notifier_unwind\":true}");
}
