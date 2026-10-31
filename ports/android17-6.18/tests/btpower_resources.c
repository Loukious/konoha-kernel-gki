#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define pr_debug(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
/* ENUMS */
struct regulator { int enabled; };
struct vreg_data { const char *name; struct regulator *reg; bool is_enabled,is_retention_supp,fmd_mode_set; int min_vol,max_vol,load_curr; struct {int init,crash;} indx; };
struct bt_power_clk_data { const char *name; void *clk; bool is_enabled; };
struct platform_pwr_data { struct vreg_data *bt_vregs,*uwb_vregs,*platform_vregs; int bt_num_vregs,uwb_num_vregs,platform_num_vregs; bool bt_sec_hw_disable,is_fmd_mode_enable; int bt_gpio_sys_rst,bt_gpio_debug; struct bt_power_clk_data *bt_chip_clk; };
static struct platform_pwr_data data,*pwr_data=&data;
static struct { int bt_state[64],uwb_state[64],platform_state[64]; } power_src;
static int steps,fail_step,disable_fail,enabled,release_calls,gpio_error,clk_enabled;
static int failure(void) { return ++steps==fail_step?-EIO:0; }
static int regulator_set_voltage(struct regulator*r,int low,int high) { (void)r;(void)low;(void)high;return failure(); }
static int regulator_set_load(struct regulator*r,int load) { (void)r;(void)load;return failure(); }
static int regulator_enable(struct regulator*r) { int ret=failure();if(!ret){r->enabled++;enabled++;}return ret; }
static int regulator_disable(struct regulator*r) { if(disable_fail){disable_fail--;return -EBUSY;}assert(r->enabled==1);r->enabled--;enabled--;return 0; }
static int regulator_get_voltage(struct regulator*r) { (void)r;return 900000; }
static int clk_prepare_enable(void*c) { (void)c;int r=failure();if(!r)clk_enabled++;return r; }
static void clk_disable_unprepare(void*c) { (void)c;assert(clk_enabled==1);clk_enabled--; }
static int gpio_is_valid(int n){return n>=0;}
static int bt_configure_gpios(int on){return on?gpio_error:0;}
static void bt_release_gpios(void){release_calls++;}
static int get_fmd_mode(void){return data.is_fmd_mode_enable;}
static int btpower_get_retenion_mode_state(void){return RETENTION_IDLE;}
static int perisec_cnss_bt_hw_disable_check(struct platform_pwr_data*d){(void)d;return 0;}
/* FUNCTIONS */
static void reset(struct vreg_data *v,struct regulator *r,int count) {
 memset(&data,0,sizeof(data));memset(v,0,count*sizeof(*v));memset(r,0,count*sizeof(*r));
 data.bt_gpio_sys_rst=1;data.bt_vregs=v;data.bt_num_vregs=count;steps=fail_step=disable_fail=enabled=release_calls=gpio_error=0;
 for(int i=0;i<count;i++){v[i]=(struct vreg_data){.name="native",.reg=r+i,.min_vol=750000,.max_vol=1040000,.load_curr=0,.is_retention_supp=true,.indx={i,i}};}
}
int main(void){
 struct vreg_data v[8];struct regulator r[8];int cases=0;
 for(int f=1;f<=24;f++){reset(v,r,8);fail_step=f;assert(bt_regulators_pwr(POWER_ENABLE)==-EIO);assert(!enabled);for(int i=0;i<8;i++)assert(!v[i].is_enabled);cases++;}
 reset(v,r,8);gpio_error=-EPROBE_DEFER;assert(bt_regulators_pwr(POWER_ENABLE)==-EPROBE_DEFER);assert(!enabled);cases++;
 reset(v,r,8);assert(!bt_regulators_pwr(POWER_ENABLE)&&enabled==8);assert(!bt_regulators_pwr(POWER_DISABLE)&&!enabled);cases++;
 reset(v,r,8);assert(!bt_regulators_pwr(POWER_ENABLE));disable_fail=1;assert(bt_regulators_pwr(POWER_DISABLE)==-EBUSY);assert(enabled==1&&v[0].is_enabled);assert(!bt_regulators_pwr(POWER_DISABLE)&&!enabled);cases++;
 reset(v,r,8);assert(!bt_regulators_pwr(POWER_ENABLE));data.is_fmd_mode_enable=true;v[0].fmd_mode_set=v[7].fmd_mode_set=true;assert(!bt_regulators_pwr(POWER_DISABLE));assert(enabled==2);data.is_fmd_mode_enable=false;assert(!bt_regulators_pwr(POWER_DISABLE)&&!enabled);cases++;
 reset(v,r,8);assert(!handle_pwr_disable_req(BT_CORE,0,0,0));assert(handle_pwr_disable_req(100,0,0,0)==-EINVAL);cases+=2;
 for(int f=1;f<=24;f++){reset(v,r,8);data.uwb_vregs=v;data.uwb_num_vregs=8;fail_step=f;assert(uwb_regulators_pwr(POWER_ENABLE)==-EIO);assert(!enabled);cases++;}
 for(int f=1;f<=24;f++){reset(v,r,8);data.platform_vregs=v;data.platform_num_vregs=8;fail_step=f;assert(platform_regulators_pwr(POWER_ENABLE)==-EIO);assert(!enabled);cases++;}
 reset(v,r,8);assert(!bt_regulators_pwr(POWER_ENABLE));steps=0;fail_step=1;assert(bt_regulators_pwr(POWER_RETENTION)==-EIO);fail_step=0;assert(!bt_regulators_pwr(POWER_DISABLE)&&!enabled);cases++;
 printf("{\"regulator_fault_and_retention_cases\":%d}\n",cases);
}
