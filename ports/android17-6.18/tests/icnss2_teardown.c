#include "icnss2_mock.h"
#define LINUX_VERSION_CODE 61800
#define KERNEL_VERSION(a,b,c) ((a)*10000+(b)*100+(c))
#define WCN7750_DEVICE_ID 2
#define WCN6750_DEVICE_ID 1
#define WCN6450_DEVICE_ID 3
#define ADRASTEA_DEVICE_ID 0
#define RAMDUMP_NUM_DEVICES 10
struct device {void*data;};struct platform_device{struct device dev;};
struct icnss_priv{struct platform_device*pdev;unsigned long state,device_id;bool stopping,psf_supported,is_slate_rfa,wpss_supported,genl_registered;spinlock_t event_lock,soc_wake_msg_lock;struct completion unblock_shutdown,smp2p_soc_wake_wait,slate_boot_complete;struct timer_list recovery_timer,wpss_ssr_timer;struct workqueue_struct *event_wq,*soc_wake_wq,*soc_update_wq;void*batt_psy,*msa0_dump_dev,*m3_dump_phyareg,*m3_dump_phydbg,*m3_dump_wmac0reg,*m3_dump_wcssdbg,*m3_dump_phyapdmem,*icnss_ramdump_class,*iommu_domain;int icnss_ramdump_dev,psf_nb;};
static struct icnss_priv *penv;static struct work_struct wpss_loader,wpss_ssr_work;static bool drained,qmi_live,ims_live,dms_live,psf_live;static int resources,battery_puts,genl_exits,notifiers;
static void*dev_get_drvdata(struct device*d){return d->data;}static void dev_set_drvdata(struct device*d,void*p){d->data=p;}
#define device_init_wakeup(...) ((void)0)
#define icnss_debugfs_destroy(...) ((void)0)
#define icnss_sysfs_destroy(...) ((void)0)
#define icnss_slate_ssr_unregister_notifier(...) (notifiers++)
#define icnss_unregister_slate_event_notifier(...) (notifiers++)
#define icnss_wpss_early_ssr_unregister_notifier(...) (notifiers++)
#define icnss_wpss_ssr_unregister_notifier(...) (notifiers++)
#define icnss_modem_ssr_unregister_notifier(...) (notifiers++)
#define icnss_pdr_unregister_notifier(...) (notifiers++)
#define icnss_reboot_unregister_notifier(...) (notifiers++)
#define unregister_rproc_restart_level_notifier(...) (notifiers++)
static void power_supply_unreg_notifier(int*n){assert(psf_live);psf_live=false;}
static void power_supply_put(void*b){assert(!psf_live&&!penv->soc_update_wq);battery_puts++;}
#undef destroy_workqueue
static void teardown_queue(struct workqueue_struct*w){assert(penv->stopping);if(w==penv->soc_update_wq)assert(!psf_live);else assert(!qmi_live&&!ims_live&&!dms_live);destroy_workqueue(w);}
#define destroy_workqueue(w) teardown_queue(w)
#undef flush_workqueue
static void flush_workqueue(struct workqueue_struct*w){assert(penv->stopping&&penv->recovery_timer.stopped&&penv->wpss_ssr_timer.stopped&&!wpss_loader.pending&&!wpss_ssr_work.pending&&notifiers>=2);assert(qmi_live);drained=true;}
static void icnss_unregister_ims_service(struct icnss_priv*p){assert(drained);ims_live=false;}
static void icnss_dms_deinit(struct icnss_priv*p){assert(drained);dms_live=false;}
static void icnss_unregister_fw_service(struct icnss_priv*p){assert(drained);qmi_live=false;}
static void release_resource(void){assert(!qmi_live&&!ims_live&&!dms_live&&!queues);resources++;}
#define icnss_destroy_ramdump_device(p) release_resource()
#define class_destroy(p) release_resource()
#define unregister_chrdev_region(p,n) release_resource()
#define icnss_genl_exit() (genl_exits++,release_resource())
#define icnss_runtime_pm_deinit(p) release_resource()
#define icnss_aop_interface_deinit(p) release_resource()
#define icnss_unregister_bus_scale(p) release_resource()
static void icnss_set_plat_priv(struct icnss_priv*p){assert(!queues);penv=p;}
#define icnss_hw_power_off(p) release_resource()
#define icnss_put_resources(p) release_resource()
/* FUNCTIONS */
int main(void){unsigned cases=0;for(int device=0;device<4;device++)for(int mask=0;mask<16;mask++){struct platform_device pdev={0};struct icnss_priv p={.pdev=&pdev,.device_id=device,.psf_supported=!!(mask&1),.is_slate_rfa=!!(mask&2),.wpss_supported=!!(mask&4),.genl_registered=!!(mask&8)};penv=&p;pdev.dev.data=&p;qmi_live=ims_live=dms_live=true;psf_live=p.psf_supported;notifiers=0;drained=false;int prior=genl_exits;p.event_wq=create_singlethread_workqueue("e");if(device)p.soc_wake_wq=create_singlethread_workqueue("s");if(p.psf_supported){p.soc_update_wq=create_singlethread_workqueue("p");p.batt_psy=&p;}p.icnss_ramdump_class=mask&1?&p:ERR_PTR(-ENOMEM);wpss_loader.pending=wpss_ssr_work.pending=1;icnss_remove(&pdev);assert(!penv&&!pdev.dev.data&&!queues&&p.stopping&&!qmi_live&&!psf_live);assert(genl_exits-prior==p.genl_registered);cases++;}printf("{\"teardown_variants\":%u,\"producers_and_work_stopped_before_resources\":true,\"power_supply_unregistered_before_drain_and_put\":true,\"optional_handles_guarded\":true}\n",cases);}
