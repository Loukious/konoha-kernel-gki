#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t u_int32_t;
#define USEC_PER_SEC 1000000
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p)>=(uintptr_t)-4095)
#define div_u64(n,d) ((n)/(d))
#define ilog2(n) (63-__builtin_clzll(n))
struct regulator { int n; };
struct fp_vreg { bool enable, IsGpio; struct regulator *mRegulator; int mPwrGpio; };
struct fp_device { void *pinctrl, *pins_reset_low, *pins_reset_high; u32 rst_low_delay, rst_high_delay; bool irq_request; int irq_enabled,irq_num; };
struct pm8941_pwrkey { int irq; bool wake_enabled; };
struct device { struct pm8941_pwrkey *p; bool wake; };
static int step,fail,irq_on,irq_off,wake_on,wake_off; static u64 slept;
static int operation(void) { return ++step==fail ? -EIO : 0; }
static int gpio_is_valid(int n) { return n>=0 && n<200; }
static int gpio_direction_output(int n,int v) { assert(gpio_is_valid(n) && (v==0 || v==1)); return operation(); }
static int regulator_enable(struct regulator *p) { assert(p); return operation(); }
static int regulator_disable(struct regulator *p) { assert(p); return operation(); }
static void usleep_range(int a,int b) { assert(a<=b); slept+=a; }
static void msleep(unsigned n) { slept+=n*1000ULL; }
static int pinctrl_select_state(void *p,void *s) { assert(p && s); return operation(); }
static void enable_irq(int n) { assert(n==17); irq_on++; }
static void disable_irq(int n) { assert(n==17); irq_off++; }
static int enable_irq_wake(int n) { assert(n==17); wake_on++; return operation(); }
static int disable_irq_wake(int n) { assert(n==17); wake_off++; return operation(); }
static struct pm8941_pwrkey *dev_get_drvdata(struct device *d) { return d->p; }
static bool device_may_wakeup(struct device *d) { return d->wake; }
/* FUNCTIONS */
int main(void) {
 for(u32 shift=6;shift<=14;shift+=8) for(u32 us=1;us<=2000000;us++) {
   u64 cycles=((u64)us*(1ULL<<shift))/1000000, copy=cycles; u32 expected=0;
   while(copy>1) { copy/=2; expected++; } assert(pm8941_debounce_code(us,shift)==expected);
 }
 struct regulator reg={0}; unsigned power_cases=0;
 for(int gpio=0;gpio<2;gpio++) for(int f=0;f<=2;f++) {
   struct fp_vreg r={.IsGpio=gpio,.mRegulator=&reg,.mPwrGpio=3}; step=0;fail=f;
   int ret=fp_power_on(&r); assert((ret!=0)==(f && f<=(gpio?2:1))); assert(r.enable==!ret); power_cases++;
   if(!ret) { step=0; fail=1; assert(fp_power_off(&r)==-EIO && r.enable); fail=0; assert(!fp_power_off(&r) && !r.enable); }
 }
 struct fp_vreg absent={0}; assert(fp_power_on(&absent)==-ENODEV); absent.IsGpio=true;absent.mPwrGpio=-1; assert(fp_power_on(&absent)==-ENODEV);
 struct fp_device fp={.pinctrl=&reg,.pins_reset_low=&step,.pins_reset_high=&fail,.rst_low_delay=20,.rst_high_delay=30,.irq_num=17};
 u32 delays[]={0,1,20,999,1000,1001,UINT32_MAX};
 for(unsigned a=0;a<7;a++) for(unsigned b=0;b<7;b++) {
   step=0;fail=0;slept=0;int ret=fp_hw_reset_delay(&fp,delays[a],delays[b]);bool valid=delays[a] && delays[a]<=1000 && delays[b] && delays[b]<=1000;
   assert((ret==0)==valid);assert(slept==(valid?((u64)delays[a]+delays[b])*1000:0));
 }
 for(int f=1;f<=2;f++) { step=0;fail=f;assert(fp_hw_reset(&fp)==-EIO); }
 assert(fp_enable_irq(&fp)==-ENODEV && fp_disable_irq(&fp)==-ENODEV);
 fp.irq_request=true; assert(!fp_enable_irq(&fp));assert(!fp_enable_irq(&fp));assert(irq_on==1);assert(!fp_disable_irq(&fp));assert(!fp_disable_irq(&fp));assert(irq_off==1);
 struct pm8941_pwrkey key={.irq=17};struct device dev={.p=&key,.wake=true};
 step=0;fail=1;assert(pm8941_pwrkey_suspend(&dev)==-EIO && !key.wake_enabled);fail=0;assert(!pm8941_pwrkey_suspend(&dev) && key.wake_enabled);assert(!pm8941_pwrkey_suspend(&dev) && wake_on==2);
 step=0;fail=1;assert(pm8941_pwrkey_resume(&dev)==-EIO && key.wake_enabled);fail=0;assert(!pm8941_pwrkey_resume(&dev) && !key.wake_enabled);assert(!pm8941_pwrkey_resume(&dev) && wake_off==2);
 printf("{\"debounce_cases\":4000000,\"power_cases\":%u,\"reset_bounds_cases\":49,\"power_reset_irq_faults\":true}\n",power_cases);
}
