#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_debug(...) ((void)0)
#define POWER_ENABLE 1
#define RESETB_GPIO_LOW 0
#define RESETB_GPIO_HIGH 1
#define BT_RESET_GPIO 0
#define BT_SW_CTRL_GPIO 1
#define msleep(n) ((void)(n))
struct platform_pwr_data {int bt_gpio_sys_rst,wl_gpio_sys_rst,bt_gpio_sw_ctrl,bt_gpio_debug,bt_gpio_resetb,sw_cntrl_gpio;bool reset_requested,resetb_requested,debug_requested;};
static struct platform_pwr_data data,*pwr_data=&data;
static struct{int platform_state[2];} power_src;
static int owned[8],values[8],steps,fail_step,xo;
static int fault(void){return ++steps==fail_step?-EIO:0;}
static int gpio_request(int n,const char*name){(void)name;assert(n>=0&&n<8&&!owned[n]);int r=fault();if(!r)owned[n]=1;return r;}
static void gpio_free(int n){assert(n>=0&&n<8&&owned[n]);owned[n]=0;}
static int gpio_direction_output(int n,int val){assert(n>=0&&n<8&&owned[n]);int r=fault();if(!r)values[n]=val;return r;}
static int gpio_direction_input(int n){assert(n>=0&&n<8);return fault();}
static int gpio_get_value_cansleep(int n){assert(n>=0&&n<8);return values[n];}
static void gpio_set_value_cansleep(int n,int val){assert(n>=0&&n<8&&owned[n]);values[n]=val;}
static int msm_gpio_mpm_wake_set(int n,int val){assert(n==3&&val==1);return fault();}
static void btpower_set_xo_clk_gpio_state(bool on){xo=on;}
/* FUNCTIONS */
static void reset(int wlan,int resetb,int sw){for(int i=0;i<8;i++)owned[i]=values[i]=0;data=(struct platform_pwr_data){.bt_gpio_sys_rst=1,.wl_gpio_sys_rst=4,.bt_gpio_sw_ctrl=sw?3:-1,.bt_gpio_debug=5,.bt_gpio_resetb=resetb?2:-1,.sw_cntrl_gpio=sw?3:-1};values[4]=wlan;values[3]=1;steps=fail_step=xo=0;}
int main(void){int cases=0;
 for(int wlan=0;wlan<2;wlan++)for(int rb=0;rb<2;rb++)for(int sw=0;sw<2;sw++){
  reset(wlan,rb,sw);assert(!bt_configure_gpios(1));int max=steps;assert(data.reset_requested);bt_release_gpios();for(int i=0;i<8;i++)assert(!owned[i]);cases++;
  for(int f=1;f<=max;f++){reset(wlan,rb,sw);fail_step=f;int rc=bt_configure_gpios(1);if(rc!=-EIO)fprintf(stderr,"wlan=%d rb=%d sw=%d f=%d max=%d rc=%d\n",wlan,rb,sw,f,max,rc);assert(rc==-EIO);bt_release_gpios();for(int i=0;i<8;i++)assert(!owned[i]);cases++;}
 }
 reset(1,1,1);assert(!bt_configure_gpios(1));assert(!bt_configure_gpios(1));assert(!bt_configure_gpios(0));bt_release_gpios();assert(!owned[1]&&!owned[2]);bt_release_gpios();cases+=2;
 printf("{\"gpio_request_unwind_and_repeat_cases\":%d}\n",cases);}
