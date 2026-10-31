#include "cnss2_mock.h"
#define CNSS_PLAT_IPC_DAEMON_QMI_CLIENT_V01 1
#define CNSS_UTILS_FMD_STATUS 0
#define PEACH_DEVICE_ID 7
#define COLOGNE_DEVICE_ID 8
#define FIG_DEVICE_ID 9
#define CNSS_RC_EP_ULTRASHORT_CHANNEL_V01 1
#define CNSS_AUX_UC_SUPPORT_V01 2
struct device { void *of_node; };
struct platform_device { struct device dev; };
struct cnss_plat_data {
 struct platform_device *plat_dev;struct timer_list fw_boot_timer;
 struct work_struct recovery_work;struct delayed_work wlan_reg_driver_work;
 struct completion power_up_complete,cal_complete,rddm_complete,recovery_complete,daemon_connected;
 struct mutex dev_lock,driver_ops_lock;struct notifier_block reboot_nb;
 void *recovery_ws,*on_chip_pmic_board_ids;unsigned device_id;bool misc_stopped,stopping,wlan_reg_work_initialized;
 spinlock_t event_lock;struct workqueue_struct *event_wq;
};
static int step,failure;static bool wake,reboot,ipc,fmd,sram;static unsigned features,flushes;
static int cnss_reboot_notifier(struct notifier_block *n,unsigned long v,void *p) { return 0; }
static bool fail(void) { return ++step==failure; }
static int device_init_wakeup(struct device *dev,bool on) { if(on && fail())return -EIO;wake=on;return 0; }
static int register_reboot_notifier(struct notifier_block *n) { assert(wake && !reboot);if(fail())return -ENOMEM;reboot=true;return 0; }
static void unregister_reboot_notifier(struct notifier_block *n) { assert(reboot && !ipc && !fmd);reboot=false; }
static void *wakeup_source_register(struct device *d,const char *name) { assert(reboot);return fail()?NULL:kzalloc(1,0); }
static void wakeup_source_unregister(void *p) { assert(!ipc && !fmd);kfree(p); }
static void cnss_daemon_connection_update_cb(void *p,bool v) { }
static void cnss_fmd_status_update_cb(void *p,bool v) { }
static int cnss_plat_ipc_register(unsigned id,void (*cb)(void*,bool),void *p) { assert(reboot && !ipc);if(fail())return -ENOSPC;ipc=true;cb(p,true);return 0; }
static void cnss_plat_ipc_unregister(unsigned id,void *p) { assert(ipc && !fmd);ipc=false; }
static int cnss_utils_register_status_notifier(unsigned id,void (*cb)(void*,bool),void *p) { assert(ipc && !fmd);if(fail())return -EBUSY;fmd=true;cb(p,true);return 0; }
static int cnss_utils_unregister_status_notifier(unsigned id,void (*cb)(void*,bool),void *p) { assert(fmd && ((struct cnss_plat_data*)p)->stopping);fmd=false;return 0; }
static void cnss_sram_dump_init(struct cnss_plat_data *p) { assert(!sram);sram=true; }
static void cnss_sram_dump_deinit(struct cnss_plat_data *p) { assert(sram);sram=false; }
static bool of_property_read_bool(void *node,const char *name) { return true; }
static int cnss_set_feature_list(struct cnss_plat_data *p,unsigned id) { features|=BIT(id);return 0; }
static void cnss_qmi_deinit(struct cnss_plat_data *p) { assert(p->stopping && p->fw_boot_timer.stopped && !p->recovery_work.pending); }
static void cnss_deinit_sol_gpio(struct cnss_plat_data *p) { assert(p->stopping); }
#undef timer_shutdown_sync
static void timer_shutdown_sync(struct timer_list *t) { assert(!ipc && !fmd && !reboot);t->stopped=1; }
#undef flush_workqueue
static void flush_workqueue(struct workqueue_struct *w) { assert(!ipc && !fmd && !reboot);flushes++; }
/* FUNCTIONS */
int main(void)
{
 struct platform_device dev={0};struct cnss_plat_data p;
 int errors[]={-EIO,-ENOMEM,-ENOMEM,-ENOSPC,-EBUSY};
 for(failure=1;failure<=5;failure++) {
  memset(&p,0,sizeof(p));p.plat_dev=&dev;step=0;
  assert(cnss_misc_init(&p)==errors[failure-1]);assert(!wake && !reboot && !ipc && !fmd && !sram && !live && p.fw_boot_timer.stopped);
 }
 failure=0;for(unsigned i=0;i<100;i++) {
  memset(&p,0,sizeof(p));p.plat_dev=&dev;p.device_id=FIG_DEVICE_ID;p.event_wq=create_singlethread_workqueue("events");
  assert(!cnss_misc_init(&p) && wake && reboot && ipc && fmd && sram);
  p.wlan_reg_work_initialized=true;p.wlan_reg_driver_work.work.pending=1;p.recovery_work.pending=1;
  p.on_chip_pmic_board_ids=kzalloc(4,0);cnss_misc_stop(&p);
  assert(p.stopping && p.misc_stopped && !p.wlan_reg_driver_work.work.pending && !p.recovery_work.pending);
  assert(p.power_up_complete.done && p.cal_complete.done && p.rddm_complete.done && p.recovery_complete.done && p.daemon_connected.done);
  assert(wake && p.recovery_ws && sram);unsigned n=flushes;cnss_misc_stop(&p);assert(n==flushes);
  cnss_misc_deinit(&p);assert(!wake && !reboot && !ipc && !fmd && !sram && !live && !p.recovery_ws && !p.on_chip_pmic_board_ids);
  destroy_workqueue(p.event_wq);
 }
 assert(features==(BIT(1)|BIT(2)));
 puts("{\"misc_acquisition_failures\":5,\"lifecycle_cycles\":100,\"callback_withdrawal_precedes_timer_and_work_drain\":true,\"stop_completes_waiters\":true,\"resources_outlive_bus_shutdown\":true}");
}
