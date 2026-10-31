#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
typedef uint64_t u64; typedef uint32_t u32; typedef uint8_t u8; typedef uintptr_t dma_addr_t;
#define GFP_KERNEL 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define PM_QOS_REC_MAX_RECORD 50
#define __rcu
#define __iomem
#define EPROBE_DEFER 517
#define BIT(x) (1u << (x))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define DWC3_EVENT_BUFFERS_SIZE 4096
#define DWC3_NUM_INT(n) (((n) >> 15) & 63)
#define DWC3_ENDPOINTS_NUM 32
#define USB_MAX_IRQ 5
#define IRQF_NO_AUTOEN BIT(0)
#define CLKFLAG_RETAIN_MEM 1
#define CLKFLAG_NORETAIN_MEM 0
#define MDWC3_POWER_COLLAPSE 2
#define USB_SPEED_HIGH 3
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define IS_ERR_OR_NULL(x) (!(x) || (uintptr_t)(x) > (uintptr_t)-4096)
struct device { int refs; struct device *parent; };
struct platform_device { struct device dev; void *data; };
struct regulator { int enabled; };
struct clk { int enabled, id; };
struct usb_irq { int irq; bool enable, requested; };
struct usb_irq_info { const char *name; unsigned long irq_type; bool required; };
static struct usb_irq_info usb_irq_info[USB_MAX_IRQ] = {{"0",0,false},{"1",0,true},{"2",0,false},{"3",0,false},{"4",0,false}};
struct dwc3 { struct device *dev, *sysdev; struct { unsigned hwparams1; } hwparams; };
struct dwc3_event_buffer { struct dwc3 *dwc; size_t length; void *buf; dma_addr_t dma; };
struct dwc3_msm {
 bool qos_rec_start; u8 qos_rec_index; u32 qos_rec_irq[50];
 struct device *dev, *gsi_dma_dev;
 unsigned num_gsi_event_buffers;
 struct dwc3_event_buffer **gsi_ev_buff;
 u64 *dummy_gsi_db; dma_addr_t dummy_gsi_db_dma;
 struct usb_irq wakeup_irq[USB_MAX_IRQ];
 struct regulator *dwc3_gdsc,*vbus_reg;
 bool gdsc_enabled,vbus_enabled;
 struct clk *xo_clk,*sleep_clk,*iface_clk,*noc_aggr_clk,*core_clk,*utmi_clk,*bus_aggr_clk;
 long core_clk_rate,core_clk_rate_hs,core_clk_rate_disconnected;
 bool in_host_mode; int max_rh_port_speed; unsigned long lpm_flags;
};
static struct dwc3_msm *owner;
static int alloc_fail,alloc_step,alloc_live;
static void *kmalloc_mock(size_t n) { if (++alloc_step == alloc_fail) return NULL; void *p=calloc(1,n?n:1); assert(p);alloc_live++;return p; }
#define kzalloc(n,gfp) kmalloc_mock(n)
#define kcalloc(n,s,gfp) kmalloc_mock((n)*(s))
static void kfree(void *p) { if(p){assert(alloc_live>0);alloc_live--;free(p);} }
static void *dma_alloc_coherent(struct device *d,size_t n,dma_addr_t *dma,int gfp) { assert(d && d->refs>0);void *p=kmalloc_mock(n);*dma=(uintptr_t)p;return p; }
static void dma_free_coherent(struct device *d,size_t n,void *p,dma_addr_t dma) {assert(d && d->refs>0);assert((uintptr_t)p==dma);kfree(p);}
static struct device *get_device(struct device *d){d->refs++;return d;}
static void put_device(struct device *d){assert(d->refs>0);d->refs--;}
static void *dev_get_drvdata(struct device *d){return owner;}
static void *platform_get_drvdata(struct platform_device *p){return p->data;}
static int irq_values[5],irq_fail,irq_requests;
static int platform_get_irq_byname_optional(struct platform_device *p,const char *name){return irq_values[atoi(name)];}
#define msm_dwc3_pwr_irq NULL
#define msm_dwc3_pwr_irq_thread NULL
static int devm_request_threaded_irq(struct device *d,int irq,void *a,void *b,unsigned flags,const char *name,void *data){assert(flags&IRQF_NO_AUTOEN);irq_requests++;return atoi(name)==irq_fail?-EIO:0;}
static int resource_fail,resource_step;
static bool fail_resource(void){return ++resource_step==resource_fail;}
static int regulator_enable(struct regulator *r){if(fail_resource())return -EIO;r->enabled++;return 0;}
static int regulator_disable(struct regulator *r){if(fail_resource())return -EIO;assert(r->enabled>0);r->enabled--;return 0;}
static int qcom_clk_set_flags(struct clk *c,int flags){return fail_resource()?-EIO:0;}
static int clk_prepare_enable(struct clk *c){if(fail_resource())return -EIO;if(c)c->enabled++;return 0;}
static void clk_disable_unprepare(struct clk *c){if(c){assert(c->enabled>0);c->enabled--;}}
static int clk_set_rate(struct clk *c,long rate){return fail_resource()?-EIO:0;}
/* FUNCTIONS */
int main(void){
 unsigned gsi_cases=0,irq_cases=0,power_cases=0,clock_cases=0;
 struct device d={.refs=1}; struct dwc3 dwc={.dev=&d,.sysdev=&d,.hwparams={63u<<15}};
 for(unsigned n=0;n<=64;n++) for(int failure=0;failure<=2*(int)n+2;failure++){
  struct dwc3_msm m={.dev=&d,.num_gsi_event_buffers=n};owner=&m;alloc_fail=failure;alloc_step=0;
  int r=dwc3_gsi_event_buf_alloc(&dwc);
  if(n>=63)assert(r==-EINVAL);else if(n==0)assert(!r);else if(failure && failure<=2*(int)n+2)assert(r==-ENOMEM);else assert(!r);
  if(!r && n){assert(m.gsi_ev_buff && m.dummy_gsi_db);int steps=alloc_step;assert(!dwc3_gsi_event_buf_alloc(&dwc));assert(steps==alloc_step);}
  dwc3_msm_free_gsi_buffers(&m);dwc3_msm_free_gsi_buffers(&m);assert(!alloc_live && d.refs==1);gsi_cases++;
 }
 for(int absent=-1;absent<5;absent++)for(int failure=-1;failure<5;failure++)for(int defer=0;defer<2;defer++){
  struct dwc3_msm m={.dev=&d};struct platform_device p={.data=&m};owner=&m;irq_fail=failure;irq_requests=0;
  for(int i=0;i<5;i++)irq_values[i]=20+i;
  if(absent>=0)irq_values[absent]=defer?-EPROBE_DEFER:-ENXIO;
  int expected=0,stop=5;
  for(int i=0;i<5;i++){if(i==absent){if(i==1||defer){expected=irq_values[i];stop=i;break;}continue;}if(i==failure){expected=-EIO;stop=i;break;}}
  assert(dwc3_msm_register_interrupts(&p)==expected);
  for(int i=0;i<5;i++)assert(m.wakeup_irq[i].requested==(i<stop && i!=absent));irq_cases++;
 }
 for(int fail=0;fail<=4;fail++){
  struct regulator r={0};struct clk c={0};struct dwc3_msm m={.dwc3_gdsc=&r,.core_clk=&c};resource_fail=fail;resource_step=0;
  int err=dwc3_msm_config_gdsc(&m,1);
  assert((err==0)==(fail!=1&&fail!=2));assert(m.gdsc_enabled==(r.enabled==1));
  resource_fail=0;assert(!dwc3_msm_config_gdsc(&m,0));assert(!r.enabled&&!m.gdsc_enabled);power_cases++;
 }
 for(int fail=0;fail<=2;fail++){
  struct regulator r={.enabled=1};struct clk c={0};struct dwc3_msm m={.dwc3_gdsc=&r,.core_clk=&c,.gdsc_enabled=true};resource_fail=fail;resource_step=0;
  int err=dwc3_msm_config_gdsc(&m,0);assert((err==0)==(fail==0));assert(m.gdsc_enabled==(r.enabled==1));resource_fail=0;assert(!dwc3_msm_config_gdsc(&m,0));assert(!r.enabled);power_cases++;
 }
 for(int fail=0;fail<=2;fail++){
  struct regulator r={0};struct dwc3_msm m={.vbus_reg=&r};resource_fail=fail;resource_step=0;
  int e=vbus_regulator_toggle(&m,true);assert((e==0)==(fail!=1));resource_fail=0;assert(!vbus_regulator_toggle(&m,true));assert(!vbus_regulator_toggle(&m,true));assert(r.enabled==1);assert(!vbus_regulator_toggle(&m,false));assert(!r.enabled);power_cases++;
 }
 for(int sleep=0;sleep<2;sleep++)for(int fail=0;fail<=8;fail++){
  struct clk c[7]={0};struct dwc3_msm m={.xo_clk=c,.sleep_clk=c+1,.iface_clk=c+2,.noc_aggr_clk=c+3,.core_clk=c+4,.utmi_clk=c+5,.bus_aggr_clk=c+6,.core_clk_rate=150000000,.core_clk_rate_disconnected=19200000};resource_fail=fail;resource_step=0;
  int r=dwc3_clk_enable_disable(&m,true,sleep);bool should_fail=fail>0&&fail<=7+sleep;assert((r<0)==should_fail);
  resource_fail=0;if(!r)assert(!dwc3_clk_enable_disable(&m,false,sleep));for(int i=0;i<7;i++)assert(!c[i].enabled);clock_cases++;
 }
 for(int start=0;start<2;start++)for(int index=0;index<256;index++){struct dwc3_msm m={.qos_rec_start=start,.qos_rec_index=index};dwc3_msm_record_irq(&m,123);for(int i=0;i<50;i++)assert(m.qos_rec_irq[i]==(start&&i==index?123u:0u));assert(m.qos_rec_index==(start&&index<50?index+1:index));}
 printf("{\"debug_index_cases\":512,\"gsi_allocation_cases\":%u,\"irq_cases\":%u,\"power_vote_cases\":%u,\"clock_unwind_cases\":%u}\n",gsi_cases,irq_cases,power_cases,clock_cases);
}
