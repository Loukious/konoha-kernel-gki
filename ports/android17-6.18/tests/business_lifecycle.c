static int sequence,fail_at,live_wq,live_wake,live_usb,live_wls,live_sysfs,live_debug,live_log,notifiers,drains,handled;
static void *allocations[32];static int allocations_count;
static struct charger_event seen[16384];
static bool stop_hook;static pthread_t active_thread;
static pthread_barrier_t reached_dispatch,release_dispatch,reached_drain;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;void *p=calloc(1,n);assert(p);allocations[allocations_count++]=p;return p;}
static void free_allocations(void){for(int n=0;n<allocations_count;n++)free(allocations[n]);allocations_count=0;}
static struct workqueue_struct *alloc_ordered_workqueue(const char *name,int flag){(void)name;(void)flag;if(fault())return NULL;assert(!live_wq);live_wq++;return calloc(1,sizeof(struct workqueue_struct));}
static void destroy_workqueue(struct workqueue_struct *q){assert(live_wq==1&&notifiers==0&&!live_sysfs&&!live_log&&!live_debug);live_wq--;free(q);}
static struct wakeup_source *wakeup_source_register(struct device *d,const char *n){(void)d;(void)n;if(fault())return NULL;assert(!live_wake);live_wake++;return calloc(1,sizeof(struct wakeup_source));}
static void wakeup_source_unregister(struct wakeup_source *w){assert(live_wake==1);live_wake--;free(w);}
static void __pm_relax(struct wakeup_source *w){assert(w&&live_wake);}
#define INIT_WORK(w,f) do{(w)->fn=f;(w)->pending=false;}while(0)
#define INIT_DELAYED_WORK(w,f) INIT_WORK(&(w)->work,f)
static bool queue_work(struct workqueue_struct *q,struct work_struct *w){assert(q&&w->fn);WRITE_ONCE(w->pending,true);return true;}
static bool mod_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay){(void)delay;return queue_work(q,&w->work);}
static void cancel_delayed_work_sync(struct delayed_work *w){assert(w->work.fn);w->work.pending=false;drains++;}
static void cancel_work_sync(struct work_struct *w){assert(w->fn);w->pending=false;if(stop_hook){pthread_barrier_wait(&reached_drain);assert(!pthread_join(active_thread,NULL));stop_hook=false;}}
static int dt_wls,dt_test,dt_error;
static int mca_parse_dts_u32(void *np,const char *name,int *out,int fallback){(void)np;(void)name;(void)fallback;*out=dt_wls;return dt_error;}
static int of_property_read_u32(void *np,const char *name,int *out){(void)np;(void)name;*out=dt_test;return dt_error;}
struct usb_psy_info *business_charger_usb_psy_init(struct device *d){assert(d->data);if(fault())return ERR_PTR(-EAGAIN);live_usb++;static struct usb_psy_info info;return &info;}
struct wireless_psy_info *business_charger_wireless_psy_init(struct device *d){(void)d;assert(0);return NULL;}
void business_charger_usb_psy_deinit(struct usb_psy_info *i){if(i){assert(live_usb==1);live_usb--;}}
void business_charger_wireless_psy_deinit(struct wireless_psy_info *i){assert(!i&&!live_wls);}
static void no_work(struct work_struct *w){(void)w;}
#define business_charger_wireless_report_status_work no_work
#define business_charger_wireless_delay_enable_rx_work no_work
#define business_charger_wireless_reset_rx_work no_work
#define business_charger_report_quick_charge_type_work no_work
#define business_charger_wireless_rerun_usb_sns_work no_work
#define business_charger_pmic_init_done_notify_work no_work
static int business_charger_sysfs_create_group(struct business_charger *c){assert(c->workqueue&&c->online_wake_lock);if(fault())return -EIO;live_sysfs++;return 0;}
static void business_charger_sysfs_remove_group(struct device *d){assert(d->data&&live_sysfs==1);live_sysfs--;}
static int table;
#define g_business_charge_debugfs_field_tbl (&table)
#define BUSINESS_CHARGER_DEBUGFS_ATTRS_SIZE 1
#define g_business_charger_log_ops table
static int mca_debugfs_create_group(const char *name,void *data,int n,void *c){(void)name;(void)data;(void)n;assert(c);if(fault())return -ENOMEM;live_debug++;return 0;}
static void mca_debugfs_remove_group(const char *name){(void)name;assert(live_debug==1);live_debug--;}
static void mca_log_charge_log_register(int id,void *ops,void *c){(void)id;(void)ops;assert(c);live_log++;}
static void mca_log_charge_log_unregister(int id,void *c){(void)id;assert(c&&live_log==1);live_log--;}
static struct notifier_block *registered[128];
static int mca_event_block_notify_register(int type,struct notifier_block *nb){assert(nb->notifier_call);if(fault())return -EAGAIN;assert(!registered[type]);registered[type]=nb;notifiers++;int v=17;assert(!notifier_to_errno(nb->notifier_call(nb,MCA_EVENT_SOC_LIMIT,&v)));return 0;}
static int mca_event_block_notify_unregister(int type,struct notifier_block *nb){assert(registered[type]==nb);registered[type]=NULL;notifiers--;return 0;}
static void business_charger_dispatch_event(struct business_charger *c,struct charger_event *e){(void)c;if(stop_hook){pthread_barrier_wait(&reached_dispatch);pthread_barrier_wait(&release_dispatch);}assert(handled<16384);seen[handled++]=*e;}
/* FUNCTIONS */
static void reset(void){sequence=fail_at=drains=handled=dt_error=0;dt_wls=STOCK_WLS;dt_test=STOCK_TEST;assert(!business_owner&&!live_wq&&!live_wake&&!live_usb&&!notifiers&&!live_sysfs&&!live_debug&&!live_log);}
static void *run_events(void *arg){struct business_charger *c=arg;business_charger_event_work(&c->event_work);return NULL;}
static void *remove_dev(void *arg){business_charger_remove(arg);return NULL;}
struct producer {struct business_charger *c;int id,accepted;};
static atomic_bool producers_done;
static void *produce(void *arg){struct producer *p=arg;for(int n=0;n<1000;n++){int val[2]={p->id,n};int ret=business_charger_add_event_node(MCA_EVENT_CP_TSHUT_FLAG,val,sizeof(val),p->c);assert(!ret||ret==-ENOSPC);if(!ret)p->accepted++;}return NULL;}
static void *consume(void *arg){struct business_charger *c=arg;while(!atomic_load(&producers_done)){business_charger_event_work(&c->event_work);sched_yield();}while(c->event_count)business_charger_event_work(&c->event_work);return NULL;}
int main(void){
 alarm(25);struct platform_device dev={0},duplicate={0};reset();assert(!business_charger_probe(&dev));int checkpoints=sequence;struct business_charger *c=dev.dev.data;assert(c->ready&&c->event_count==7&&notifiers==6&&live_usb==1&&!live_wls);business_charger_event_work(&c->event_work);assert(handled==7);
 assert(business_charger_probe(&duplicate)==-EBUSY&&!duplicate.dev.data);business_charger_shutdown(&dev);assert(drains==6);business_charger_remove(&dev);assert(drains==6);free_allocations();
 for(int n=1;n<=checkpoints;n++){reset();fail_at=n;assert(business_charger_probe(&dev)<0);assert(!dev.dev.data&&!business_owner&&!live_wq&&!live_wake&&!live_usb&&!notifiers&&!live_sysfs&&!live_debug&&!live_log);free_allocations();}
 reset();dt_wls=1;assert(business_charger_probe(&dev)==-EOPNOTSUPP&&!dev.dev.data);free_allocations();reset();dt_wls=2;assert(business_charger_probe(&dev)==-EINVAL);free_allocations();
 reset();assert(!business_charger_probe(&dev));c=dev.dev.data;business_charger_event_work(&c->event_work);handled=0;int value=42;
 assert(business_charger_add_event_node(1,NULL,4,c)==-EINVAL);assert(business_charger_add_event_node(1,&value,9,c)==-EINVAL);assert(business_charger_event_size(MCA_EVENT_FG_OTA_PROCESS)==4&&business_charger_event_size(MCA_EVENT_SBU_LPD_CHANGE)==4);
 int fifo=0;for(int round=0;round<3;round++){for(int n=0;n<64;n++)assert(!business_charger_add_event_node(round,&n,4,c));assert(business_charger_add_event_node(9,&value,4,c)==-ENOSPC);business_charger_event_work(&c->event_work);for(int n=0;n<64;n++){assert(seen[fifo].event==(unsigned)round&&seen[fifo].data[0]==n&&seen[fifo].data[1]==0);fifo++;}}
 pthread_t writer[4],reader;struct producer producers[4];int before=handled,total=0;atomic_store(&producers_done,false);assert(!pthread_create(&reader,NULL,consume,c));for(int n=0;n<4;n++){producers[n]=(struct producer){c,n,0};assert(!pthread_create(&writer[n],NULL,produce,&producers[n]));}for(int n=0;n<4;n++){assert(!pthread_join(writer[n],NULL));total+=producers[n].accepted;}atomic_store(&producers_done,true);assert(!pthread_join(reader,NULL));assert(handled-before==total);
 assert(!business_charger_add_event_node(1,&value,4,c));stop_hook=true;pthread_barrier_init(&reached_dispatch,NULL,2);pthread_barrier_init(&release_dispatch,NULL,2);pthread_barrier_init(&reached_drain,NULL,2);assert(!pthread_create(&active_thread,NULL,run_events,c));pthread_barrier_wait(&reached_dispatch);pthread_t removing;assert(!pthread_create(&removing,NULL,remove_dev,&dev));pthread_barrier_wait(&release_dispatch);pthread_barrier_wait(&reached_drain);assert(!pthread_join(removing,NULL));assert(c->stopping&&business_charger_add_event_node(1,&value,4,c)==-ENODEV);free_allocations();
 printf("{\"probe_failure_points\":%d,\"registered_notifiers\":6,\"delayed_work_drains\":6,\"active_event_removal\":true,\"shutdown_remove_idempotent\":true,\"fifo_cases\":%d,\"overflow_cases\":3,\"concurrent_enqueue_attempts\":4000,\"accepted_events_drained\":%d,\"missing_payload_checks\":true,\"ota_and_sbu_payload_sizes\":true,\"wired_only_wireless_rejected\":true}\n",checkpoints,fifo,total);
}
