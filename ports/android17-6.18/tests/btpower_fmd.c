#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_FMD_ENABLE 1
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define OTHER_FMD_SUPPORTED_BT_SOC 3
#define PEACH_SOC_VERSION_2_0 2
#define POWER_ENABLE 1
#define POWER_DISABLE 0
struct bt_power_clk_data{bool is_enabled;};
struct platform_pwr_data{struct bt_power_clk_data*bt_chip_clk;bool is_fmd_mode_enable;};
static struct bt_power_clk_data clk;static struct platform_pwr_data data={.bt_chip_clk=&clk},*pwr_data=&data;
static struct {unsigned char fmdOperation,socFwVer;short int rebootStatus,fmdCycles;} fmdStruct;
enum FmdOperation {ENABLE_FMD,DISABLE_FMD,UPDATE_SOC_VER};
static int clk_error,wlan_error,nvmem_error,notified,clock_disables;
static int bt_clk_enable(struct bt_power_clk_data*c){if(clk_error)return clk_error;c->is_enabled=true;return 0;}
static int bt_clk_disable(struct bt_power_clk_data*c){c->is_enabled=false;clock_disables++;return 0;}
static int vote_wlan_reg_for_fmd(void){return wlan_error;}
static void cnss_utils_fmd_status(bool on){notified=on;}
static int fmd_set_sdam_bit(unsigned char n){assert(n==1);return nvmem_error;}
static int fmd_reboot_on_usb_detection(unsigned char n){(void)n;return nvmem_error;}
static int fmd_write_stop_counter(unsigned char n){(void)n;return nvmem_error;}
/* FUNCTIONS */
int main(void){int cases=0;
 fmdStruct.fmdOperation=UPDATE_SOC_VER;
 for(int soc=-1;soc<=4;soc++){clk.is_enabled=data.is_fmd_mode_enable=false;fmdStruct.socFwVer=soc;int rc=perform_fmd_operation();if(soc>=1&&soc<=3)assert(!rc&&clk.is_enabled&&data.is_fmd_mode_enable);else assert(rc==-EINVAL&&!clk.is_enabled&&!data.is_fmd_mode_enable);cases++;}
 fmdStruct.socFwVer=2;data.is_fmd_mode_enable=clk.is_enabled=false;clk_error=-EIO;assert(perform_fmd_operation()==-EIO&&!data.is_fmd_mode_enable);clk_error=0;cases++;
 wlan_error=-EAGAIN;notified=0;assert(perform_fmd_operation()==-EAGAIN&&!clk.is_enabled&&!data.is_fmd_mode_enable&&!notified&&clock_disables==1);cases++;
 clk.is_enabled=true;assert(perform_fmd_operation()==-EAGAIN&&clk.is_enabled&&!data.is_fmd_mode_enable&&clock_disables==1);cases++;
 wlan_error=0;assert(!perform_fmd_operation()&&clk.is_enabled&&data.is_fmd_mode_enable&&notified);assert(!perform_fmd_operation());cases+=2;
 fmdStruct.fmdOperation=ENABLE_FMD;fmdStruct.rebootStatus=-1;fmdStruct.fmdCycles=-1;assert(!perform_fmd_operation());nvmem_error=-ENODEV;assert(perform_fmd_operation()==-ENODEV);nvmem_error=0;cases+=2;
 fmdStruct.rebootStatus=256;assert(perform_fmd_operation()==-EINVAL);fmdStruct.rebootStatus=0;fmdStruct.fmdCycles=256;assert(perform_fmd_operation()==-EINVAL);cases+=2;
 fmdStruct.fmdOperation=DISABLE_FMD;assert(!perform_fmd_operation()&&!data.is_fmd_mode_enable);cases++;
 printf("{\"FMD_enable_failure_and_repeated_update_cases\":%d}\n",cases);}
