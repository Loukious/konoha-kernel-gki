#include "mock.h"
enum dma_data_direction {DMA_BIDIRECTIONAL=0,DMA_TO_DEVICE=1,DMA_FROM_DEVICE=2,DMA_NONE=3};
/* PUBLIC_TYPES */
#define MHI_INVALID_BRSTMODE(m) ((m)!=2&&(m)!=3)
#define BHIE_RXVECSTATUS_OFFS 0x78
#define MHI_TIMEOUT_MS 1000
#define MHI_MAX_MTU 65535
#define MHI_PM_M0 1
#define MHI_PM_M2 2
struct device {int x;};struct mhi_ring_element{u64 ptr;u32 data[2];};struct mhi_buf_info{u8 data[64];};struct db_cfg{u32 pollcfg,brstmode;bool reset_req;void (*process_db)(void);};struct mhi_ring{u32 elements;};struct mhi_chan{const char *name;u32 chan,er_index,dir,type,ee_mask;bool configured,lpm_notify,offload_ch,pre_alloc,wake_capable;struct mhi_ring tre_ring,buf_ring;struct db_cfg db_cfg;struct list_head node;};struct mhi_event{u32 er_index,intmod,irq,chan,priority,data_type;struct mhi_ring ring;struct db_cfg db_cfg;struct mhi_chan *mhi_chan;bool hw_ring,cl_manage,offload_ev;void (*process_event)(void);};struct mhi_controller{struct device *cntrl_dev;u32 max_chan,nr_irqs,total_ev_rings,hw_ev_rings,sw_ev_rings,timeout_ms,buffer_len,db_access;size_t reg_len;struct mhi_chan *mhi_chan;struct mhi_event *mhi_event;struct list_head lpm_chans;void *regs,*bhie;bool bounce_buf;};
static void mhi_db_brstmode(void){}
static void mhi_db_brstmode_disable(void){}
static void mhi_process_data_event_ring(void){}
static void mhi_process_ctrl_ev_ring(void){}
static void mhi_process_misc_bw_ev_ring(void){}
static void mhi_process_misc_tsync_ev_ring(void){}
#define CONFIG_MHI_SATELLITE 1
#define CONFIG_MHI_BUS_MISC 1
#define IS_ENABLED(x) (x)
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,33)
/* CONSUMER_CONFIGS */
/* FUNCTIONS */
static int run(struct mhi_controller_config *conf,int expect){struct mhi_controller c={.nr_irqs=3,.reg_len=4096,.regs=(void *)4096};int r=parse_config(&c,conf);assert((r==0)==(!expect));if(!r){assert(c.hw_ev_rings+c.sw_ev_rings==conf->num_events);vfree(c.mhi_chan);kfree(c.mhi_event);}
assert(!allocations);return r;}
int main(void){const struct mhi_controller_config *real[]={&cnss_mhi_config_default,&cnss_mhi_config_no_diag,&cnss_mhi_config_genoa,&cnss_mhi_config_no_satellite};unsigned cases=0;for(unsigned n=0;n<ARRAY_SIZE(real);n++){struct mhi_controller_config copy=*real[n];run(&copy,0);cases++;}
struct mhi_channel_config ch[2]={{.name="A",.num_elements=32,.dir=DMA_TO_DEVICE,.doorbell=2},{.name="B",.num=1,.num_elements=32,.dir=DMA_FROM_DEVICE,.doorbell=3}};struct mhi_event_config ev={.num_elements=32,.irq=1,.channel=U32_MAX,.mode=2,.data_type=MHI_ER_CTRL};struct mhi_controller_config conf={.max_channels=32,.num_channels=2,.ch_cfg=ch,.num_events=1,.event_cfg=&ev};run(&conf,0);
for(u32 e=0;e<70;e++){ch[0].num_elements=e;run(&conf,e<2);cases++;}ch[0].num_elements=32;for(u32 n=0;n<40;n++){ch[0].num=n;run(&conf,n==1||n>=32);cases++;}ch[0].num=0;for(u32 n=0;n<65;n++){ch[0].local_elements=n;run(&conf,n&&n<32);cases++;}ch[0].local_elements=0;for(u32 irq=0;irq<5;irq++){ev.irq=irq;run(&conf,irq>=3);cases++;}ev.irq=1;for(u32 n=0;n<8;n++){ev.priority=n;run(&conf,n>2);cases++;}ev.priority=0;ev.client_managed=true;run(&conf,1);ev.channel=1;run(&conf,0);ev.channel=3;run(&conf,1);ev.client_managed=false;ev.channel=U32_MAX;ch[0].name="";run(&conf,1);ch[0].name="A";ch[0].event_ring=1;run(&conf,1);ch[0].event_ring=0;ch[0].auto_queue=true;run(&conf,1);ch[0].auto_queue=false;ch[0].dir=99;run(&conf,1);ch[0].dir=DMA_TO_DEVICE;ev.data_type=99;run(&conf,1);ev.data_type=MHI_ER_CTRL;conf.bhie_offset=4092;run(&conf,1);conf.bhie_offset=1;run(&conf,1);conf.bhie_offset=512;run(&conf,0);conf.bhie_offset=0;conf.max_channels=257;run(&conf,1);conf.max_channels=32;conf.ch_cfg=NULL;run(&conf,1);conf.ch_cfg=ch;
for(int fault=1;fault<=2;fault++){allocation_call=0;fail_alloc=fault;assert(run(&conf,1)==-ENOMEM);cases++;}
fail_alloc=0;printf("{\"configuration_fault_cases\":%u,\"pinned_cnss_configurations_accepted\":4,\"channel_event_and_allocation_checks\":true}\n",cases);}
