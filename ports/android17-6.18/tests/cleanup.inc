static struct mutex wcd_usbss_provider_lock;
static struct wcd_usbss_ctxt *wcd_usbss_provider;
static int wcd_usbss_srcu,wcd_usbss_attribute_group;
static const char *supply_names[]={"vdd-usb-cp"};
#define rcu_access_pointer(p) (p)
#define RCU_INIT_POINTER(p,v) ((p)=(v))
static int clean_events[32],clean_count,regulator_error;
static void event(int e){assert(clean_count<(int)ARRAY_SIZE(clean_events));clean_events[clean_count++]=e;}
static void synchronize_srcu(int *s){(void)s;assert(!wcd_usbss_provider&&!wcd_usbss_provider_lock.held);event(1);}
static void sysfs_remove_group(struct kobject *k,int *g){(void)k;(void)g;event(2);}
static void kobject_put(struct kobject *k){(void)k;event(3);}
static void unregister_ucsi_glink_notifier(struct notifier_block *n){(void)n;event(4);}
static void disable_irq_wake(int irq){(void)irq;event(5);}
static void devm_free_irq(struct device *d,int irq,void *p){(void)d;(void)irq;(void)p;event(6);}
static void kthread_stop(struct task_struct *t){(void)t;event(7);}
static void cancel_work_sync(struct work_struct *w){(void)w;event(8);}
static void devm_regmap_qti_debugfs_unregister(struct regmap *r){(void)r;event(9);}
static void pm_runtime_barrier(struct device *d){(void)d;event(10);}
static void pm_runtime_dont_use_autosuspend(struct device *d){(void)d;event(11);}
static void pm_runtime_disable(struct device *d){(void)d;event(12);}
static void device_init_wakeup(struct device *d,bool enabled){(void)d;assert(!enabled);event(13);}
static int regulator_bulk_disable(size_t n,struct regulator_bulk_data *r){(void)r;assert(n==1);event(14);return regulator_error;}
/* CLEANUP_DEFINITIONS */
static uint64_t cleanup_cases;
static void test_cleanup(void){
 struct device dev={0};struct i2c_client client={.irq=7};struct regmap map={0};struct kobject k={0};struct task_struct task={0};
 for(unsigned int flags=0;flags<128;flags++)for(int error=0;error<2;error++){
  struct wcd_usbss_ctxt p=fresh(&map,&dev);p.client=&client;p.surge_kobject=flags&1?&k:NULL;p.ucsi_registered=!!(flags&2);p.irq_registered=!!(flags&4);p.irq_wake_enabled=!!(flags&8);p.surge_thread=flags&16?&task:NULL;p.debugfs_registered=!!(flags&32);p.supplies_enabled=!!(flags&64);p.surge_enable=false;
  wcd_usbss_provider=&p;clean_count=0;regulator_error=error?-EIO:0;wcd_usbss_cleanup(&p);
  assert(clean_events[0]==1&&p.removing&&p.stopped&&!p.surge_thread&&!p.surge_kobject&&!p.irq_registered&&!p.ucsi_registered&&!p.debugfs_registered);
  for(int i=1;i<clean_count;i++)assert(clean_events[i]>clean_events[i-1]);
  assert(p.supplies_enabled==((flags&64)&&error));int before=clean_count;
  regulator_error=0;wcd_usbss_cleanup(&p);assert(!p.supplies_enabled);
  assert(clean_count==before+((flags&64)&&error));if(clean_count!=before)assert(clean_events[before]==14);
  before=clean_count;wcd_usbss_cleanup(&p);assert(before==clean_count);cleanup_cases++;
 }
}
