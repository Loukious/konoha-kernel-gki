#include "slim_mock.h"
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{bool used[256];};struct idr{void*slot[256];};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
/* PUBLIC HEADER */
/* PRIVATE HEADER */
static int allocations,frees,status_calls,addr_ret,set_ret,pm_ret,pm_votes;
static struct slim_device device;
static int ida_alloc_max(struct ida*i,unsigned max,int f){for(unsigned n=0;n<=max;n++)if(!i->used[n]){i->used[n]=true;allocations++;return n;}return -ENOSPC;}
static void ida_free(struct ida*i,unsigned id){assert(i->used[id]);i->used[id]=false;frees++;}
static int get_address(struct slim_controller*c,struct slim_eaddr*ea,u8*addr){assert(c->lock.held);if(addr_ret)return addr_ret;*addr=23;return 0;}
static int set_address(struct slim_controller*c,struct slim_eaddr*ea,u8 addr){assert(c->lock.held);return set_ret;}
static void slim_device_update_status(struct slim_device*d,enum slim_device_status status){d->status=status;status_calls++;}
static int pm_runtime_get_sync(struct device*d){pm_votes++;return pm_ret;}
static void pm_runtime_put_noidle(struct device*d){assert(pm_votes>0);pm_votes--;}
static void pm_runtime_mark_last_busy(struct device*d){}
static void pm_runtime_put_autosuspend(struct device*d){assert(pm_votes>0);pm_votes--;}
#define pm_runtime_set_suspended(d) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
struct slim_device*slim_get_device(struct slim_controller*c,struct slim_eaddr*ea){get_device(&device.dev);return &device;}
/* FUNCTIONS */
int main(void){struct slim_controller ctrl={.set_laddr=set_address};device.ctrl=&ctrl;device.dev.refs=1;u8 output=99;unsigned cases=0;assert(!slim_device_alloc_laddr(&device,true)&&device.is_laddr_valid&&device.laddr==0&&allocations==1);assert(!slim_device_alloc_laddr(&device,true)&&allocations==1);slim_report_absent(&device);assert(!device.is_laddr_valid&&frees==1&&device.status==SLIM_DEVICE_STATUS_DOWN);slim_report_absent(&device);assert(frees==1);set_ret=-EIO;assert(slim_device_alloc_laddr(&device,true)==-EIO&&!device.is_laddr_valid&&allocations==2&&frees==2);set_ret=0;assert(slim_device_alloc_laddr(&device,false)==-EINVAL);ctrl.get_laddr=get_address;addr_ret=-EAGAIN;assert(slim_device_alloc_laddr(&device,false)==-EAGAIN&&!device.is_laddr_valid);addr_ret=0;assert(!slim_get_logical_addr(&device)&&device.laddr==23&&device.is_laddr_valid);slim_report_absent(&device);assert(frees==2);ctrl.get_laddr=NULL;
for(int scenario=0;scenario<5;scenario++){device.is_laddr_valid=false;output=99;pm_ret=0;ctrl.sched.clk_state=SLIM_CLK_ACTIVE;set_ret=0;int expected=0;if(scenario==0){pm_ret=-EIO;expected=-EIO;}if(scenario==1){ctrl.sched.clk_state=SLIM_CLK_PAUSED;expected=-EHOSTDOWN;}if(scenario==2){set_ret=-EINVAL;expected=-EINVAL;}if(scenario==4)pm_ret=1;assert(slim_device_report_present(&ctrl,&device.e_addr,&output)==expected&&!pm_votes&&device.dev.refs==1);if(!expected){assert(output==device.laddr&&device.is_laddr_valid);output=99;assert(!slim_device_report_present(&ctrl,&device.e_addr,&output)&&output==device.laddr);slim_report_absent(&device);}else assert(output==99&&!device.is_laddr_valid);cases++;}assert(allocations==frees&&!ctrl.lock.held);printf("{\"report_failure_cases\":%u,\"address_output_initialized\":true,\"failed_assignment_frees_id\":true,\"repeated_allocation_and_absence\":true,\"pm_votes_balanced\":true}\n",cases);}
