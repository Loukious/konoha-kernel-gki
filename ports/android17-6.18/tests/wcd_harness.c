#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int atomic_t;
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define BIT(n) (1U<<(n))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define GFP_KERNEL 0
#define I2C_M_RD 1
#define I2C_M_TEN 16
#define I2C_CLIENT_TEN 16
#define TYPEC_MAX_ACCESSORY 3
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_warn(...) ((void)0)
#define fallthrough __attribute__((fallthrough))
struct mutex {int held;};
static void mutex_lock(struct mutex *m){assert(!m->held);m->held++;}
static void mutex_unlock(struct mutex *m){assert(m->held==1);m->held--;}
struct device {void *data;};
struct i2c_client {void *adapter;u16 addr,flags;int irq;};
struct i2c_msg {u16 addr,flags,len;u8 *buf;};
struct work_struct {int unused;};struct notifier_block {int unused;};
struct blocking_notifier_head {int unused;};
struct regulator_bulk_data {int unused;};
struct kobject {int unused;};struct task_struct {int unused;};
/* DRIVER_TYPES */
static void *dev_get_drvdata(struct device *d){return d->data;}
static bool allocation_fail;
static void *kmalloc(size_t n,int flags){(void)flags;return allocation_fail?NULL:malloc(n);}
#define kfree free
static u8 bus_memory[65536],bus_page;
static unsigned int bus_calls;static int response1=INT_MAX,response2=INT_MAX;
static int i2c_transfer(void *adapter,struct i2c_msg *m,int n){
 (void)adapter;bus_calls++;int scripted=bus_calls==1?response1:bus_calls==2?response2:INT_MAX;
 if(scripted!=INT_MAX&&scripted!=n)return scripted;
 assert(n==1||n==2);u8 reg=m[0].buf[0];
 if(n==1){assert(m[0].len>=2);for(unsigned int i=1;i<m[0].len;i++,reg++){if(!reg)bus_page=m[0].buf[i];else bus_memory[((unsigned int)bus_page<<8)+reg]=m[0].buf[i];}}
 else {assert(m[0].len==1&&m[1].len==1&&(m[1].flags&I2C_M_RD));m[1].buf[0]=reg?bus_memory[((unsigned int)bus_page<<8)+reg]:bus_page;}
 return n;
}
struct regmap {unsigned int values[WCD_USBSS_NUM_REGISTERS];};
struct op {unsigned int kind,reg,mask,value;};
static struct op trace[512],reference_trace[512];static unsigned int operations;
static unsigned int fail_operation,dirty_marks,sync_count;static int sync_error,pm_error,pm_gets,pm_puts,awake;
static int record(unsigned int kind,unsigned int reg,unsigned int mask,unsigned int value){assert(operations<ARRAY_SIZE(trace));trace[operations++]=(struct op){kind,reg,mask,value};return fail_operation==operations?-EIO:0;}
static int regmap_write(struct regmap *r,unsigned int reg,unsigned int value){assert(reg<WCD_USBSS_NUM_REGISTERS);int ret=record(1,reg,255,value);if(!ret)r->values[reg]=value;return ret;}
static int regmap_update_bits(struct regmap *r,unsigned int reg,unsigned int mask,unsigned int value){assert(reg<WCD_USBSS_NUM_REGISTERS);int ret=record(2,reg,mask,value);if(!ret)r->values[reg]=(r->values[reg]&~mask)|(value&mask);return ret;}
static int regmap_update_bits_base(struct regmap *r,unsigned int reg,unsigned int mask,unsigned int value,bool *change,bool async,bool force){(void)change;(void)async;assert(force);int ret=record(3,reg,mask,value);if(!ret)r->values[reg]=(r->values[reg]&~mask)|(value&mask);return ret;}
static int regmap_read(struct regmap *r,unsigned int reg,unsigned int *v){assert(reg<WCD_USBSS_NUM_REGISTERS);int ret=record(4,reg,0,0);if(!ret){if(reg==WCD_USBSS_SWITCH_STATUS1)*v=(r->values[WCD_USBSS_SWITCH_SELECT0]&0xc0)==0x40?0x48:0x84;else *v=r->values[reg];}return ret;}
static int regcache_sync(struct regmap *r){(void)r;sync_count++;return sync_error;}
static void regcache_mark_dirty(struct regmap *r){(void)r;dirty_marks++;}
static int pm_runtime_resume_and_get(struct device *d){(void)d;pm_gets++;return pm_error;}
static void pm_stay_awake(struct device *d){(void)d;awake++;}
static void pm_relax(struct device *d){(void)d;if(awake)awake--;}
static void pm_runtime_mark_last_busy(struct device *d){(void)d;}
static void pm_runtime_put_autosuspend(struct device *d){(void)d;pm_puts++;}
static void usleep_range(unsigned int a,unsigned int b){assert(b>=a);}
/* STAGED_DEFINITIONS */
static struct wcd_usbss_ctxt *wcd_usbss_ctxt_;static int audio_fsm_mode;
/* BASELINE_DEFINITIONS */
static uint64_t transport_cases,short_cases,parity_cases,error_cases,pm_cases;
static struct wcd_usbss_ctxt fresh(struct regmap *map,struct device *dev){struct wcd_usbss_ctxt c={.regmap=map,.dev=dev};return c;}
static void reset_runtime(void){pm_error=pm_gets=pm_puts=awake=0;sync_error=sync_count=dirty_marks=0;fail_operation=operations=0;}
static void test_transport(void){
 struct device dev={0};struct i2c_client client={.addr=0x42};struct wcd_usbss_ctxt c={.client=&client,.dev=&dev};dev.data=&c;
 u8 values[WCD_USBSS_NUM_REGISTERS],output[WCD_USBSS_NUM_REGISTERS],regbuf[3];for(unsigned int i=0;i<sizeof(values);i++)values[i]=(u8)(131+i*17);
 unsigned int lengths[]={1,2,7,31,255,256,257,511};
 for(unsigned int start=0;start<=WCD_USBSS_MAX_REGISTER;start++)for(unsigned int nidx=0;nidx<ARRAY_SIZE(lengths);nidx++)for(int write=0;write<2;write++){
  size_t count=lengths[nidx];u16 address=start;memcpy(regbuf+1,&address,sizeof(address));response1=response2=INT_MAX;bus_calls=0;c.prev_pg_valid=false;
  for(unsigned int i=0;i<sizeof(bus_memory);i++)bus_memory[i]=(u8)(i*7+23);
  int ret=wcd_usbss_regmap_transfer(&dev,regbuf+1,2,write?values:output,count,write);
  if(count>WCD_USBSS_MAX_REGISTER-start+1)assert(ret==-EINVAL&&!bus_calls);
  else {assert(!ret&&!c.io_lock.held);for(unsigned int i=0;i<count;i++){unsigned int a=start+i;if(write){if(a%256)assert(bus_memory[a]==values[i]);}else assert(output[i]==(a%256?(u8)(a*7+23):(u8)(a>>8)));}}
  transport_cases++;
 }
 int codes[]={-EIO,-ETIMEDOUT,0,1,2,3};
 for(int read=0;read<2;read++)for(unsigned int a=0;a<ARRAY_SIZE(codes);a++)for(unsigned int b=0;b<ARRAY_SIZE(codes);b++){
  response1=codes[a];response2=codes[b];bus_calls=0;c.prev_pg_valid=true;u8 v=0x81;int expected=read?2:1;
  int ret=read?wcd_usbss_i2c_read_device(&c,1,1,&v):wcd_usbss_i2c_write_device(&c,1,&v,1);
  if(codes[a]==expected)assert(!ret&&bus_calls==1);else if(codes[b]==expected)assert(!ret&&bus_calls==2);else assert(ret==(codes[b]<0?codes[b]:-EIO)&&!c.prev_pg_valid&&bus_calls==2);short_cases++;
 }
 response1=response2=INT_MAX;allocation_fail=true;bus_calls=0;assert(wcd_usbss_i2c_write_device(&c,1,values,1)==-ENOMEM&&!bus_calls);allocation_fail=false;
 size_t invalid[]={0,SIZE_MAX,UINT_MAX};for(unsigned int i=0;i<ARRAY_SIZE(invalid);i++){u16 reg=1;bus_calls=0;assert(wcd_usbss_regmap_transfer(&dev,&reg,2,values,invalid[i],false)==-EINVAL&&!bus_calls);transport_cases++;}
 for(size_t n=0;n<=2;n++)assert(regmap_bus_write(&dev,values,n)==-EINVAL);
}
static void test_parity(void){
 struct device dev={0};struct regmap original={0},current={0};
 for(unsigned int cable=0;cable<256;cable++)for(unsigned int version=0;version<2;version++)for(unsigned int features=0;features<4;features++)for(unsigned int type=0;type<WCD_USBSS_CABLE_TYPE_MAX;type++)for(unsigned int connect=0;connect<2;connect++){
  reset_runtime();memset(&original,0,sizeof(original));current=original;
  struct wcd_usbss_ctxt old=fresh(&original,&dev),now=fresh(&current,&dev);old.cable_status=now.cable_status=cable;old.version=now.version=version;
  old.standby_enable=now.standby_enable=features&1;old.usb_sbu_compliance=now.usb_sbu_compliance=!!(features&2);old.is_in_standby=now.is_in_standby=!(cable&1);
  old.wcd_standby_status=now.wcd_standby_status=WCD_USBSS_USB_MODE_SET;now.audio_fsm_mode=audio_fsm_mode=WCD_USBSS_AUDIO_MANUAL;wcd_usbss_ctxt_=&old;
  int ret=baseline_wcd_usbss_switch_update(type,connect);unsigned int n=operations;memcpy(reference_trace,trace,n*sizeof(*trace));
  reset_runtime();struct wcd_usbss_saved_state saved;assert(!wcd_usbss_begin(&now,&saved));int r=wcd_usbss_switch_update_ctx(&now,type,connect);r=wcd_usbss_end(&now,&saved,r);
  assert(r==ret&&n==operations&&!memcmp(reference_trace,trace,n*sizeof(*trace))&&!memcmp(&original,&current,sizeof(original)));
  assert(old.cable_status==now.cable_status&&old.is_in_standby==now.is_in_standby&&old.wcd_standby_status==now.wcd_standby_status&&audio_fsm_mode==now.audio_fsm_mode);assert(!now.runtime_env_counter&&pm_gets==pm_puts&&!awake);parity_cases++;
 }
}
static void test_errors(void){
 struct device dev={0};struct regmap map={0};
 for(unsigned int type=0;type<WCD_USBSS_CABLE_TYPE_MAX;type++)for(unsigned int failure=1;failure<80;failure++){
  reset_runtime();memset(&map,0,sizeof(map));struct wcd_usbss_ctxt p=fresh(&map,&dev);p.cable_status=0x10;p.version=1;p.standby_enable=true;fail_operation=failure;
  struct wcd_usbss_saved_state saved;assert(!wcd_usbss_begin(&p,&saved));int ret=wcd_usbss_switch_update_ctx(&p,type,WCD_USBSS_CABLE_CONNECT);ret=wcd_usbss_end(&p,&saved,ret);
  if(operations==failure){assert(ret==-EIO&&p.needs_sync&&dirty_marks==1&&p.cable_status==0x10&&!p.is_in_standby&&p.standby_enable);}
  else assert(ret==0&&operations<failure&&!p.needs_sync);
  assert(pm_gets==pm_puts&&!p.runtime_env_counter&&!awake);error_cases++;
 }
 for(int type=-2;type<=WCD_USBSS_CABLE_TYPE_MAX+2;type++)if(type<0||type>=WCD_USBSS_CABLE_TYPE_MAX){reset_runtime();struct wcd_usbss_ctxt p=fresh(&map,&dev);assert(wcd_usbss_switch_update_ctx(&p,type,1)==-EINVAL&&!operations);error_cases++;}
 reset_runtime();struct wcd_usbss_ctxt p=fresh(&map,&dev);p.removing=true;struct wcd_usbss_saved_state saved;assert(wcd_usbss_begin(&p,&saved)==-ENODEV&&!p.switch_update_lock.held);p.removing=false;p.suspended=true;assert(wcd_usbss_begin(&p,&saved)==-EBUSY&&!p.switch_update_lock.held);
}
static void test_runtime(void){
 struct device dev={0};struct regmap map={0};
 for(int failpm=0;failpm<3;failpm++)for(int dirty=0;dirty<2;dirty++)for(int failsync=0;failsync<2;failsync++){
  reset_runtime();struct wcd_usbss_ctxt p=fresh(&map,&dev);p.needs_sync=dirty;pm_error=failpm==1?-EACCES:failpm==2?-EIO:0;sync_error=failsync?-EIO:0;
  int ret=acquire_runtime_env(&p);
  if(failpm){assert(ret==pm_error&&pm_gets==8&&!pm_puts&&!p.runtime_env_counter&&!awake);}
  else if(dirty&&failsync){assert(ret==-EIO&&pm_gets==1&&pm_puts==1&&!p.runtime_env_counter&&!awake&&p.needs_sync);}
  else {assert(ret==0&&p.runtime_env_counter==1&&awake==1);assert(!acquire_runtime_env(&p)&&p.runtime_env_counter==2&&pm_gets==1);release_runtime_env(&p);release_runtime_env(&p);assert(!p.runtime_env_counter&&pm_puts==1&&!awake);release_runtime_env(&p);assert(pm_puts==1);}
  pm_cases++;
 }
 reset_runtime();struct wcd_usbss_ctxt p=fresh(&map,&dev);p.runtime_env_counter=INT_MAX;assert(acquire_runtime_env(&p)==-EOVERFLOW&&!pm_gets&&p.runtime_env_counter==INT_MAX);pm_cases++;
}
/* CLEANUP_TEST */
int main(void){test_transport();test_parity();test_errors();test_runtime();test_cleanup();printf("{\"transport_cases\":%llu,\"short_transfer_pairs\":%llu,\"vendor_register_trace_cases\":%llu,\"register_failure_cases\":%llu,\"runtime_cases\":%llu,\"cleanup_cases\":%llu}\n",(unsigned long long)transport_cases,(unsigned long long)short_cases,(unsigned long long)parity_cases,(unsigned long long)error_cases,(unsigned long long)pm_cases,(unsigned long long)cleanup_cases);}
