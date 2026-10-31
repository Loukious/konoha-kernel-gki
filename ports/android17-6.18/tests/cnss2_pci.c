#include "cnss2_mock.h"
#define QCA6174_DEVICE_ID 1
#define QCA6290_DEVICE_ID 2
#define QCA6390_DEVICE_ID 3
#define QCN7605_DEVICE_ID 4
#define QCA6490_DEVICE_ID 5
#define KIWI_DEVICE_ID 6
#define MANGO_DEVICE_ID 7
#define PEACH_DEVICE_ID 8
#define COLOGNE_DEVICE_ID 9
#define FIG_DEVICE_ID 10
#define CNSS_PCI_PROBE_DONE 1
struct mhi_controller { void *irq;bool registered; };
struct cnss_plat_data { void *bus_priv;unsigned long driver_state; };
struct device { struct cnss_plat_data *priv; };
struct pci_dev { struct device dev;int device;void *data; };
struct cnss_pci_data { struct mhi_controller *mhi_ctrl;int device_id;struct timer_list dev_rddm_timer,boot_debug_timer;struct delayed_work time_sync_work; };
static bool stopped;static unsigned released,bus_disabled,msi_disabled;
static struct cnss_pci_data *cnss_get_pci_priv(struct pci_dev *d) { return d->data; }
static void cnss_set_pci_priv(struct pci_dev *d,void *p) { d->data=p; }
static struct cnss_plat_data *cnss_bus_dev_to_plat_priv(struct device *d) { return d->priv; }
static int cnss_pci_unregister_driver_hdlr(struct cnss_pci_data *p) { assert(p->dev_rddm_timer.stopped && p->boot_debug_timer.stopped);stopped=true;return 0; }
static void mhi_unregister_controller(struct mhi_controller *m) { assert(stopped && m->registered);m->registered=false; }
static void mhi_free_controller(struct mhi_controller *m) { assert(!m->registered && !m->irq);kfree(m); }
static void release(struct cnss_pci_data *p) { assert(stopped && !p->mhi_ctrl);released++; }
#define cnss_pci_free_aux_mem(p) release(p)
#define cnss_pci_free_tme_lite_mem(p) release(p)
#define cnss_pci_free_tme_opt_file_mem(p) release(p)
#define cnss_pci_free_m3_mem(p) release(p)
#define cnss_pci_free_fw_mem(p) release(p)
#define cnss_pci_free_qdss_mem(p) release(p)
#define cnss_pci_wake_gpio_deinit(p) ((void)0)
static void cnss_pci_disable_msi(struct cnss_pci_data *p) { assert(!p->mhi_ctrl);msi_disabled++; }
static void cnss_pci_disable_bus(struct cnss_pci_data *p) { assert(msi_disabled>bus_disabled);bus_disabled++; }
#define cnss_dereg_pci_event(p) ((void)0)
#define cnss_pci_deinit_smmu(p) ((void)0)
#define cnss_unregister_ramdump(p) ((void)0)
#define cnss_unregister_subsys(p) ((void)0)
/* FUNCTIONS */
int main(void)
{
 struct cnss_pci_data pci={0};struct cnss_plat_data plat={0};struct pci_dev dev={.dev={&plat},.data=&pci};
 cnss_pci_unregister_mhi(&pci);
 dev.data=NULL;cnss_pci_remove(&dev);assert(!released);
 for(int id=1;id<=10;id++) {
  memset(&pci,0,sizeof(pci));pci.device_id=id;dev.device=id;dev.data=&pci;plat.bus_priv=&pci;set_bit(CNSS_PCI_PROBE_DONE,&plat.driver_state);stopped=false;
  if(id!=QCA6174_DEVICE_ID) { pci.mhi_ctrl=kzalloc(sizeof(*pci.mhi_ctrl),0);pci.mhi_ctrl->irq=kzalloc(16,0);pci.mhi_ctrl->registered=true; }
  cnss_pci_remove(&dev);assert(stopped && !pci.mhi_ctrl && !dev.data && !plat.bus_priv && !test_bit(CNSS_PCI_PROBE_DONE,&plat.driver_state) && !live);
  cnss_pci_remove(&dev);
 }
 assert(released==60 && bus_disabled==10);
 puts("{\"pci_device_cases\":10,\"mhi_unregister_before_dma_free\":true,\"timers_stopped_before_shutdown\":true,\"stale_drvdata_cleared\":true,\"hardware_operations_mocked\":true}");
}
