#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define smp_wmb() __sync_synchronize()
#define DEFINE_MUTEX(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define mutex_lock(p) assert(!pthread_mutex_lock(p))
#define mutex_unlock(p) assert(!pthread_mutex_unlock(p))
#define THIS_MODULE NULL
#define DWC3_GCTL_PRTCAP_DEVICE 2
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
#define dbg_event(...) ((void)0)
#define pr_err(...) ((void)0)
#define USB_ROLE_NONE 0
#define USB_ROLE_HOST 1
#define USB_ROLE_DEVICE 2
struct device;
struct dev_pm_ops { int (*prepare)(struct device*); void (*complete)(struct device*); int (*suspend)(struct device*); };
struct platform_device;
struct device_driver { const char *name; const struct dev_pm_ops *pm; void *owner; void (*remove)(struct platform_device*); };
struct device { struct device *parent; struct device_driver *driver; void *data; struct { bool direct_complete; } power; bool enabled,suspended; };
struct platform_device { struct device dev; };
#define platform_driver device_driver
#define to_platform_driver(d) (d)
struct dwc3_ep { struct { int maxpacket,maxpacket_limit; } endpoint; };
struct dwc3 { struct device *dev; unsigned current_dr_role; struct dwc3_ep *eps[32]; };
struct usb_gadget { struct device dev; struct dwc3 *dwc; };
struct work_struct { int queued; };
struct usb_role_switch { int id; };
enum usb_role { ROLE_NONE, ROLE_HOST, ROLE_DEVICE };
struct dwc3_msm { bool removing,in_host_mode; void *dwc3_wq; struct work_struct resume_work; struct usb_role_switch *role_switch; struct dwc3_msm *next; int role; };
static struct dwc3_msm *dwc3_msm_role_devices;
#define list_for_each_entry(pos,head,member) for(pos=*(head);pos;pos=pos->next)
static DEFINE_MUTEX(dwc3_msm_role_lock);
static DEFINE_MUTEX(dwc3_msm_pm_ops_lock);
static DEFINE_MUTEX(dwc3_msm_residency_lock);
static bool dwc3_msm_resident;
static struct dev_pm_ops dwc3_msm_core_pm_ops,dwc3_msm_host_pm_ops;
static const struct dev_pm_ops *dwc3_msm_original_core_pm,*dwc3_msm_original_host_pm;
static void (*dwc3_msm_original_core_remove)(struct platform_device*);
static int remove_calls,dma_frees;
static void original_remove(struct platform_device *p){remove_calls++;}
static void dwc3_msm_free_gsi_buffers(struct dwc3_msm *m){assert(remove_calls);dma_frees++;}
static int original_pre_calls,original_complete_calls,original_return,resume_calls,module_refs,gadget_casts;
static bool try_module_get(void *m){return true;}
static void __module_get(void *m){module_refs++;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static int pm_runtime_enabled(struct device *d){return d->enabled;}
static int pm_runtime_suspended(struct device *d){return d->suspended;}
static int pm_runtime_resume(struct device *d){resume_calls++;return 0;}
static void queue_work(void *wq,struct work_struct *w){w->queued++;}
static int original_prepare(struct device *d){original_pre_calls++;return original_return;}
static void original_complete(struct device *d){original_complete_calls++;}
static int original_suspend(struct device *d){return 19;}
static enum usb_role dwc3_msm_get_role(struct dwc3_msm *m){return m->role;}
static int dwc3_msm_set_role(struct dwc3_msm *m,enum usb_role r){m->role=r;return 0;}
static struct dwc3 *gadget_to_dwc(struct usb_gadget *g){gadget_casts++;return g->dwc;}
struct pt_regs { uintptr_t regs[8]; };
struct kretprobe_instance { int unused; };
struct kretprobe { struct { const char *symbol_name; } kp; bool registered; };
static struct kretprobe dwc3_msm_probes[16];
static int probe_fail,probe_calls,probe_unregisters;
static int register_kretprobe(struct kretprobe *p){int i=probe_calls++;assert(!p->registered);if(i==probe_fail)return -EIO;p->registered=true;return 0;}
static void unregister_kretprobe(struct kretprobe *p){assert(p->registered);p->registered=false;probe_unregisters++;}
static int dwc3_host_prepare(struct device *dev);
static void dwc3_host_complete(struct device *dev);
static int dwc3_core_prepare(struct device *dev);
static void dwc3_core_complete(struct device *dev);
/* FUNCTIONS */
int main(void){
 unsigned pm_cases=0,hook_cases=0;
 const struct dev_pm_ops ops={original_prepare,original_complete,original_suspend};
 struct device_driver glue_drv={.name="msm-dwc3"},core_drv={.name="dwc3",.pm=&ops,.remove=original_remove},host_drv={"xhci-hcd",&ops},foreign_drv={"other",&ops};
 struct dwc3_msm m={0};struct device glue={.driver=&glue_drv,.data=&m},core={.parent=&glue,.driver=&core_drv},host={.parent=&core,.driver=&host_drv},foreign={.driver=&foreign_drv};struct dwc3 dwc={.dev=&core};core.data=&dwc;
 assert(!dwc3_msm_override_pm_ops(&core,false));assert(!dwc3_msm_override_pm_ops(&host,true));assert(module_refs==1);assert(!dwc3_msm_override_pm_ops(&core,false));assert(module_refs==1);assert(dwc3_msm_override_pm_ops(&foreign,false)==-EBUSY);assert(core.driver->pm->suspend==original_suspend);
 for(int own=0;own<2;own++)for(int removing=0;removing<2;removing++)for(int enabled=0;enabled<2;enabled++)for(int suspended=0;suspended<2;suspended++)for(int role=0;role<2;role++)for(int err=-1;err<=1;err++){
  core.parent=own?&glue:&foreign;m.removing=removing;core.enabled=host.enabled=enabled;core.suspended=host.suspended=suspended;dwc.current_dr_role=role?DWC3_GCTL_PRTCAP_DEVICE:0;original_return=err;original_pre_calls=0;
  int r=dwc3_core_prepare(&core);int expected=err?err:(!own||removing)?0:(enabled&&suspended)?1:(role&&!suspended)?-EBUSY:0;assert(r==expected&&original_pre_calls==1);
  original_pre_calls=0;r=dwc3_host_prepare(&host);expected=err?err:(!own||removing)?0:(enabled&&suspended)?1:0;assert(r==expected&&original_pre_calls==1);
  m.resume_work.queued=0;original_complete_calls=0;dwc3_core_complete(&core);assert(original_complete_calls==1);assert(m.resume_work.queued==(own&&!removing));host.power.direct_complete=true;resume_calls=0;original_complete_calls=0;dwc3_host_complete(&host);assert(original_complete_calls==1);assert(resume_calls==(own&&!removing));pm_cases++;
 }
 core.parent=&glue;m.removing=true;struct platform_device cp={.dev=core};remove_calls=dma_frees=0;core_drv.remove(&cp);assert(remove_calls==1&&dma_frees==1);cp.dev.parent=&foreign;core_drv.remove(&cp);assert(remove_calls==2&&dma_frees==1);
 core.parent=&glue;m.removing=false;struct usb_role_switch sw={1};m.role_switch=&sw;dwc3_msm_role_devices=&m;assert(!dwc3_msm_usb_role_switch_set_role(&sw,USB_ROLE_HOST));assert(dwc3_msm_usb_role_switch_get_role(&sw)==USB_ROLE_HOST);m.removing=true;assert(dwc3_msm_usb_role_switch_set_role(&sw,USB_ROLE_DEVICE)==-ENODEV);m.removing=false;dwc3_msm_role_devices=NULL;assert(dwc3_msm_usb_role_switch_set_role(&sw,USB_ROLE_DEVICE)==-ENODEV);assert(dwc3_msm_usb_role_switch_get_role(&sw)==USB_ROLE_NONE);
 struct dwc3_ep eps[32];for(int i=0;i<32;i++){eps[i].endpoint.maxpacket=eps[i].endpoint.maxpacket_limit=i+1;dwc.eps[i]=i%3?&eps[i]:NULL;}
 struct usb_gadget g={.dev={.parent=&core},.dwc=&dwc};struct pt_regs regs={.regs={(uintptr_t)&g}};assert(!entry_usb_add_gadget(NULL,&regs));for(int i=0;i<32;i++)assert(eps[i].endpoint.maxpacket==((i>=2&&i%3)?1024:i+1));g.dev.parent=&foreign;g.dwc=NULL;gadget_casts=0;assert(!entry_usb_add_gadget(NULL,&regs));assert(!gadget_casts);g.dev.parent=NULL;assert(!entry_usb_add_gadget(NULL,&regs));assert(!gadget_casts);
 regs.regs[0]=(uintptr_t)&dwc;assert(!entry_dwc3_host_exit(NULL,&regs));core.parent=&foreign;assert(entry_dwc3_host_exit(NULL,&regs)==1);
 for(int failure=-1;failure<16;failure++){probe_fail=failure;probe_calls=probe_unregisters=0;int r=dwc3_msm_kretprobe_init();assert(r==(failure<0?0:-EIO));assert(probe_unregisters==(failure<0?0:failure));if(!r)dwc3_msm_kretprobe_exit();for(int i=0;i<16;i++)assert(!dwc3_msm_probes[i].registered);hook_cases++;}
 printf("{\"core_remove_order_cases\":2,\"pm_scope_cases\":%u,\"hook_registration_cases\":%u,\"gadget_scope_cases\":3,\"role_detach_cases\":4}\n",pm_cases,hook_cases);
}
