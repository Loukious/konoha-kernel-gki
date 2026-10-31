#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define IS_ERR_OR_NULL(p) (!(p))
#define READ_ONCE(x) (x)
#define __iomem
#define fallthrough __attribute__((fallthrough))
#define CONFIG_PINCTRL 1
#define dev_err(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
typedef uint32_t u32;
typedef uint64_t resource_size_t;
struct mutex {int ignored;};
struct device {void *pins;};
struct usb_phy { struct device *dev; unsigned flags; };
struct regulator { int id, enabled, load, min, max; };
struct clk {int id, enabled, parent;};
struct reset_control {int id, asserted;};
static struct {int type,id,a,b;} events[4096], expected[4096];
static unsigned calls,fail_at,fail_second,events_len,expected_len;
static int event(int type,int id,int a,int b) {
 assert(events_len<ARRAY_SIZE(events));
 events[events_len].type=type;events[events_len].id=id;events[events_len].a=a;events[events_len].b=b;events_len++;
 calls++;return calls==fail_at ? -71 : calls==fail_second ? -72 : 0;
}
static void trace_reset(void) { calls=events_len=fail_at=fail_second=0; }
static int regulator_enable(struct regulator *r) {int e=event(1,r->id,0,0);if(!e)r->enabled++;return e;}
static int regulator_disable(struct regulator *r) {int e=event(2,r->id,0,0);if(!e){assert(r->enabled>0);r->enabled--;}return e;}
static int regulator_set_load(struct regulator *r,int n) {int e=event(3,r->id,n,0);if(!e)r->load=n;return e;}
static int regulator_set_voltage(struct regulator *r,int a,int b) {int e=event(4,r->id,a,b);if(!e){r->min=a;r->max=b;}return e;}
static int clk_prepare_enable(struct clk *c) {if(!c)return 0;int e=event(5,c->id,0,0);if(!e)c->enabled++;return e;}
static void clk_disable_unprepare(struct clk *c) {if(!c)return;event(6,c->id,0,0);assert(c->enabled>0);c->enabled--;}
static int clk_set_parent(struct clk *c,struct clk *p) {if(!c)return 0;int e=event(7,c->id,p->id,0);if(!e)c->parent=p->id;return e;}
static int reset_control_assert(struct reset_control *r) {int e=event(8,r->id,0,0);if(!e)r->asserted=1;return e;}
static int reset_control_deassert(struct reset_control *r) {int e=event(9,r->id,0,0);if(!e)r->asserted=0;return e;}
static uint32_t mmio[0x3000/4];
static uint32_t readl_relaxed(const void *p) {ptrdiff_t i=(const uint32_t *)p-mmio;assert(i>=0&&i<ARRAY_SIZE(mmio));event(10,i,0,0);return mmio[i];}
static void writel_relaxed(u32 v,void *p) {ptrdiff_t i=(uint32_t *)p-mmio;assert(i>=0&&i<ARRAY_SIZE(mmio));event(11,i,v,0);mmio[i]=v;}
#define wmb() ((void)event(12,0,0,0))
#define mb() ((void)event(13,0,0,0))
/* TYPES */
static void usb_qmp_powerup_phy(struct msm_ssphy_qmp *phy);
static void baseline_usb_qmp_powerup_phy(struct msm_ssphy_qmp *phy);
/* FUNCTIONS */
/* BASELINE */
/* DT_ARRAYS */
static struct regulator regs[4];
static struct clk clocks[7];
static struct reset_control resets[3];
static struct device dev;
static void setup(struct msm_ssphy_qmp *p, bool gdsc, bool refgen) {
 memset(p,0,sizeof(*p));memset(regs,0,sizeof(regs));memset(clocks,0,sizeof(clocks));memset(resets,0,sizeof(resets));
 for(int i=0;i<4;i++)regs[i].id=i+1;
 for(int i=0;i<7;i++)clocks[i].id=i+1;
 for(int i=0;i<3;i++)resets[i].id=i+1;
 p->phy.dev=&dev;p->base=mmio;p->base_size=sizeof(mmio);p->vdd=&regs[0];p->core_ldo=&regs[1];p->refgen=refgen?&regs[2]:NULL;p->usb3_dp_phy_gdsc=gdsc?&regs[3]:NULL;
 p->vdd_levels[1]=880000;p->vdd_levels[2]=880000;p->vdd_max_uA=47000;p->core_voltage_levels[1]=p->core_voltage_levels[2]=1200000;p->core_max_uA=30000;p->refgen_levels[1]=p->refgen_levels[2]=1200000;
 p->ref_clk_src=&clocks[0];p->ref_clk=&clocks[1];p->com_aux_clk=&clocks[2];p->aux_clk=&clocks[3];p->cfg_ahb_clk=&clocks[4];p->pipe_clk=&clocks[5];p->pipe_clk_mux=&clocks[6];p->pipe_clk_ext_src=&clocks[5];
 p->phy_reset=&resets[0];p->phy_phy_reset=&resets[1];p->global_phy_reset=&resets[2];
 p->phy_type=USB3_AND_DP;p->phy_reg=dt_offsets;p->reg_offset_cnt=ARRAY_SIZE(dt_offsets);p->qmp_phy_init_seq=dt_sequence;p->init_seq_len=ARRAY_SIZE(dt_sequence);
 trace_reset();memset(mmio,0,sizeof(mmio));
}
static void no_votes(struct msm_ssphy_qmp *p) { assert(!p->power_enabled&&!p->gdsc_enabled);for(int i=0;i<3;i++)assert(!p->supply_votes[i]);for(int i=0;i<4;i++)assert(!regs[i].enabled&&!regs[i].load&&!regs[i].min); }
static void save_trace(void) {expected_len=events_len;memcpy(expected,events,events_len*sizeof(*events));}
static void equal_trace(void) {assert(events_len==expected_len);assert(!memcmp(events,expected,events_len*sizeof(*events)));}
int main(void) {
 struct msm_ssphy_qmp p; unsigned power_cases=0,clock_cases=0,reset_cases=0,table_cases=0,trace_cases=0;
 for(int options=0;options<4;options++) {
  setup(&p,options&1,options&2);assert(!baseline_msm_ssusb_qmp_ldo_enable(&p,1));assert(!baseline_msm_ssusb_qmp_ldo_enable(&p,0));save_trace();
  setup(&p,options&1,options&2);assert(!msm_ssusb_qmp_ldo_enable(&p,1));assert(!msm_ssusb_qmp_ldo_enable(&p,0));equal_trace();no_votes(&p);trace_cases++;
  setup(&p,options&1,options&2);assert(!msm_ssusb_qmp_ldo_enable(&p,1));unsigned steps=calls;
  for(unsigned f=1;f<=steps;f++) for(unsigned secondary=0;secondary<5;secondary++) {
   setup(&p,options&1,options&2);fail_at=f;fail_second=secondary?f+secondary:0;assert(msm_ssusb_qmp_ldo_enable(&p,1)==-71);assert(!p.power_enabled);
   trace_reset();assert(!msm_ssusb_qmp_ldo_enable(&p,0));no_votes(&p);assert(!msm_ssusb_qmp_ldo_enable(&p,1));assert(!msm_ssusb_qmp_ldo_enable(&p,0));no_votes(&p);power_cases++;
  }
  for(unsigned f=1;f<=steps;f++) {
   setup(&p,options&1,options&2);assert(!msm_ssusb_qmp_ldo_enable(&p,1));trace_reset();fail_at=f;assert(msm_ssusb_qmp_ldo_enable(&p,0)==-71);trace_reset();assert(!msm_ssusb_qmp_ldo_enable(&p,0));no_votes(&p);power_cases++;
  }
 }
 setup(&p,1,1);baseline_msm_ssphy_qmp_enable_clks(&p,true);baseline_msm_ssphy_qmp_enable_clks(&p,false);save_trace();
 setup(&p,1,1);assert(!msm_ssphy_qmp_enable_clks(&p,true));assert(!msm_ssphy_qmp_enable_clks(&p,false));equal_trace();trace_cases++;
 for(unsigned f=1;f<=7;f++) {
  setup(&p,1,1);fail_at=f;assert(msm_ssphy_qmp_enable_clks(&p,true)==-71);assert(!p.clk_enabled&&!p.clock_votes);for(int i=0;i<7;i++)assert(!clocks[i].enabled);
  trace_reset();assert(!msm_ssphy_qmp_enable_clks(&p,true));assert(!msm_ssphy_qmp_enable_clks(&p,false));assert(!p.clock_votes);clock_cases++;
 }
 for(unsigned f=1;f<=4;f++) for(int combo=0;combo<2;combo++) {
  setup(&p,1,1);fail_at=f;int ret=combo?msm_ssphy_qmp_dp_combo_reset(&p.phy):msm_ssphy_qmp_reset(&p.phy);assert(ret==-71);reset_cases++;
 }
 setup(&p,1,1);assert(!qmp_validate_tables(&p));table_cases++;
 for(unsigned n=0;n<=USB3_PHY_REG_MAX+1;n++) {p.reg_offset_cnt=n;int ret=qmp_validate_tables(&p);assert((ret==0)==(n>=13&&n<=14));table_cases++;}
 setup(&p,1,1);
 for(unsigned v=0;v<0x3100;v++) {u32 old=dt_sequence[0];dt_sequence[0]=v;assert((qmp_validate_tables(&p)==0)==(!(v&3)&&v<=0x3000-4));dt_sequence[0]=old;table_cases++;}
 p.init_seq_len=3;assert(qmp_validate_tables(&p)==-EINVAL);table_cases++;
 for(unsigned flags=0;flags<2048;flags++) {
  setup(&p,1,1);p.phy.flags=flags;
  baseline_usb_qmp_update_portselect_phymode(&p);baseline_configure_phy_regs(&p.phy,(struct qmp_reg_val *)dt_sequence);baseline_msm_ssusb_qmp_enable_autonomous(&p,1);baseline_msm_ssusb_qmp_enable_autonomous(&p,0);save_trace();
  memset(mmio,0,sizeof(mmio));trace_reset();usb_qmp_update_portselect_phymode(&p);configure_phy_regs(&p.phy,(struct qmp_reg_val *)dt_sequence);msm_ssusb_qmp_enable_autonomous(&p,1);msm_ssusb_qmp_enable_autonomous(&p,0);equal_trace();trace_cases++;
 }
 printf("{\"power_cases\":%u,\"clock_cases\":%u,\"reset_cases\":%u,\"table_cases\":%u,\"vendor_trace_cases\":%u}\n",power_cases,clock_cases,reset_cases,table_cases,trace_cases);
}
