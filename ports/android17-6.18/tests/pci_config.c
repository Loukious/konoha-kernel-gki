#include "mock.h"
#define PCI_CFG_SPACE_EXP_SIZE 4096
#define PCIBIOS_DEVICE_NOT_FOUND 1
#define PCIBIOS_BAD_REGISTER_NUMBER 2
#define RD 0
#define WR 1
#define MSM_PCIE_RES_DM_CORE 0
#define MSM_PCIE_RES_CONF 1
#define MSM_PCIE_LINK_ENABLED 1
#define PCIE_LINK_DOWN U32_MAX
struct resource{u64 size;};struct msm_pcie_res_info_t{void *base;struct resource *resource;};struct msm_pcie_dev_t{u32 rc_idx;int cfg_lock,link_status;bool cfg_access,config_recovery,driver_probed;void *dm_core,*conf;int link_recover_wq;struct msm_pcie_res_info_t res[2];};struct pci_bus{int number;struct msm_pcie_dev_t *dev;};
#define PCIE_BUS_PRIV_DATA(b) ((b)->dev)
static u64 resource_size(struct resource *r){return r->size;}
static bool linkup=true;static unsigned reads,writes,bdfs,scheduled;
static bool msm_pcie_is_link_up(struct msm_pcie_dev_t *d){return linkup;}
static void msm_pcie_cfg_bdf(struct msm_pcie_dev_t *d,int b,u32 df){bdfs++;}
static u32 readl_relaxed(void *p){u32 v;memcpy(&v,p,4);reads++;return v;}
static void msm_pcie_write_reg(void *base,u32 off,u32 val){memcpy((u8 *)base+off,&val,4);writes++;}
static void schedule_work(int *p){scheduled++;}
/* FUNCTIONS */
int main(void){u8 dm[4096],conf[4096];for(int i=0;i<4096;i++)dm[i]=conf[i]=i;struct resource r={4096};struct msm_pcie_dev_t d={.cfg_access=true,.driver_probed=true,.link_status=1,.dm_core=dm,.conf=conf,.res={{dm,&r},{conf,&r}}};struct pci_bus bus={.dev=&d};unsigned cases=0;
int sizes[]={-1,0,1,2,3,4,5,8,INT_MAX};for(int b=0;b<2;b++){bus.number=b;for(int w=-8;w<4105;w++)for(unsigned n=0;n<ARRAY_SIZE(sizes);n++){int z=sizes[n];bool valid=(z==1||z==2||z==4)&&w>=0&&w<=4096-z&&!(w&(z-1));u32 v=123;unsigned rd=reads,wr=writes;int ret=msm_pcie_oper_conf(&bus,0,RD,w,z,&v);assert(!locked);if(valid){assert(!ret);u32 expected=0;memcpy(&expected,dm+w,z);assert(v==expected);assert(reads==rd+1);}else{assert(ret==2&&v==U32_MAX&&reads==rd&&writes==wr);}cases++;}}
for(u64 span=0;span<=4096;span++){r.size=span;u32 v=0;int ret=msm_pcie_oper_conf(&bus,0,RD,4092,4,&v);assert((!ret)==(span==4096));cases++;}r.size=4096;bus.number=0;u32 v=1;assert(msm_pcie_oper_conf(&bus,1,RD,0,4,&v)==1);d.cfg_access=false;assert(msm_pcie_oper_conf(&bus,0,RD,0,4,&v)==1);d.cfg_access=true;d.link_status=0;assert(msm_pcie_oper_conf(&bus,0,RD,0,4,&v)==1);d.link_status=1;bus.number=1;linkup=false;assert(msm_pcie_oper_conf(&bus,0,RD,0,4,&v)==1);linkup=true;v=0xab;assert(!msm_pcie_oper_conf(&bus,0,WR,7,1,&v));assert(conf[7]==0xab&&conf[6]==6&&conf[8]==8);bus.dev=NULL;assert(msm_pcie_oper_conf(&bus,0,RD,0,4,&v)==1);assert(!scheduled);printf("{\"configuration_boundaries\":%u,\"invalid_accesses_do_not_touch_mmio\":true}\n",cases);}
