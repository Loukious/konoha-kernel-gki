#include "mock.h"
#define IRQ_i_CFG 0x110
#define IRQ_i_CFG_IRQ_ENABLE BIT(3)
#define IRQ_i_CFG_DISTANCE 4
#define IRQ_i_GP_IRQ_DISTANCE 0x14
#define IRQ_TYPE_EDGE_RISING 1
#define IRQ_TYPE_EDGE_FALLING 2
#define IRQ_TYPE_EDGE_BOTH 3
#define IRQ_TYPE_LEVEL_HIGH 4
#define IRQ_TYPE_LEVEL_LOW 8
#define WARN_ON(x) ((void)0)
#define IORESOURCE_MEM 1
#define IS_ERR(p) ((uintptr_t)(p)>UINTPTR_MAX-4096)
#define PTR_ERR(p) ((long)(p))
struct irq_map{u32 gpio,mux,irq;};struct pdc_match_data{const struct irq_map *map;u32 size,irq_select_offset;};
enum pdc_irq_config_bits{PDC_LEVEL_LOW=0,PDC_EDGE_FALLING=2,PDC_LEVEL_HIGH=4,PDC_EDGE_RISING=6,PDC_EDGE_DUAL=7};
struct resource{u64 size;};struct device{int x;};struct platform_device{struct device dev;struct resource *res;void *data;const struct pdc_match_data *match;};
static const struct pdc_match_data *d;static int pdc_lock;static void *pcie_pdc_base;static u8 regs[0x8000];static bool map_failure;static unsigned writes;
static const struct pdc_match_data *of_device_get_match_data(struct device *p){return container_of(p,struct platform_device,dev)->match;}
static struct resource *platform_get_resource(struct platform_device *p,int t,int n){return p->res;}
static u64 resource_size(struct resource *r){return r->size;}
static void *devm_platform_ioremap_resource(struct platform_device *p,int n){return map_failure?(void *)(intptr_t)-ENOMEM:regs;}
static void platform_set_drvdata(struct platform_device *p,void *data){p->data=data;}
static void *platform_get_drvdata(struct platform_device *p){return p->data;}
static void writel_relaxed(u32 v,void *p){assert((u8 *)p>=regs&&(u8 *)p+4<=regs+sizeof(regs));memcpy(p,&v,4);writes++;}
/* FUNCTIONS */
int main(void){struct irq_map map={118,69,10};struct pdc_match_data match={&map,1,0x4800};struct resource res={0x4804};struct platform_device p={.res=&res,.match=&match};unsigned cases=0;
assert(pcie_pdc_cfg_irq(118,1,true)==-ENODEV);p.match=NULL;assert(qcom_pcie_pdc_probe(&p)==-EINVAL);p.match=&match;p.res=NULL;assert(qcom_pcie_pdc_probe(&p)==-ENODEV);p.res=&res;
for(u64 sz=0;sz<=0x4804;sz++){res.size=sz;int r=qcom_pcie_pdc_probe(&p);assert((r==0)==(sz==0x4804));if(!r)qcom_pcie_pdc_remove(&p);cases++;}
map_failure=true;assert(qcom_pcie_pdc_probe(&p)==-ENOMEM);assert(!d&&!pcie_pdc_base);map_failure=false;assert(!qcom_pcie_pdc_probe(&p));struct platform_device duplicate=p;duplicate.data=NULL;assert(qcom_pcie_pdc_probe(&duplicate)==-EBUSY);qcom_pcie_pdc_remove(&duplicate);assert(d);
for(int i=0;i<5;i++){int types[]={1,2,3,4,8},cfg[]={6,2,7,4,0};for(int en=0;en<2;en++){assert(!pcie_pdc_cfg_irq(118,types[i],en));u32 mux,v;memcpy(&mux,regs+0x4800,4);memcpy(&v,regs+0x138,4);assert(mux==(en?69:0));assert(v==(u32)(cfg[i]|(en?8:0)));cases++;}}
unsigned old=writes;assert(pcie_pdc_cfg_irq(117,1,true)==-EINVAL);assert(pcie_pdc_cfg_irq(118,0,true)==-EINVAL);assert(old==writes);qcom_pcie_pdc_remove(&p);assert(pcie_pdc_cfg_irq(118,1,true)==-ENODEV);assert(!locked);printf("{\"mapped_span_cases\":%u,\"probe_faults_and_tuna_mapping\":true}\n",cases);}
