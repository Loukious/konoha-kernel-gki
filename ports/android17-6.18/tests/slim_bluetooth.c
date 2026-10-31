#include "slim_mock.h"
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{int unused;};struct idr{void*slot[256];};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
#define IS_ENABLED(x) 1
#define U8_MAX 255
#define SNDRV_PCM_STREAM_PLAYBACK 0
#define SNDRV_PCM_STREAM_CAPTURE 1
#define kcalloc(n,z,g) kzalloc((n)*(z),g)
#define SOC_ENUM_SINGLE_EXT_DECL(n,a) int n
#define BTFMSLIM_INFO(...) ((void)0)
#define BTFMSLIM_ERR(...) ((void)0)
#define BTFMSLIM_DBG(...) ((void)0)
/* PUBLIC HEADER */
/* BT HEADER */
/* BT INTERFACE HEADER */
/* BT SOC CONSTANTS */
#define DELAY_FOR_PORT_OPEN_MS 200
static void msleep(int x){}
static int btpower_get_chipset_version(void){return -1;}
static int btfm_num_ports_open,bt_soc_enable_status;int btfm_feedback_ch_setting;static u8 usecase_codec;
struct hwep_data{struct device*dev;};
struct master_hwep_configurations{u8 stream_id,device_id,bit_width,codectype,direction,num_channels,chan_num;u32 sample_rate;};
struct slim_stream_runtime{bool prepared,active;};
static int fail_prepare,fail_enable,fail_disable,fail_disconnect,vendor_fail,vendor_down_fail;static unsigned ports;
static int vendor(struct btfmslim*p,u8 port,u8 rx,u8 enable){if(enable){ports|=BIT(port);if(vendor_fail)return -EIO;}else{if(vendor_down_fail)return -EIO;ports&=~BIT(port);}return 0;}
struct slim_stream_runtime*slim_stream_allocate(struct slim_device*d,const char*n){void*p=kzalloc(sizeof(struct slim_stream_runtime),0);return p?p:ERR_PTR(-ENOMEM);}
int slim_stream_prepare(struct slim_stream_runtime*r,struct slim_stream_config*c){r->prepared=true;return fail_prepare?-EIO:0;}
int slim_stream_enable(struct slim_stream_runtime*r){r->active=true;return fail_enable?-EIO:0;}
int slim_stream_disable(struct slim_stream_runtime*r){if(fail_disable)return -EIO;r->active=false;return 0;}
int slim_stream_unprepare_disconnect_port(struct slim_stream_runtime*r,bool dis,bool done){if(fail_disconnect)return -EIO;if(done)r->prepared=false;return 0;}
int slim_stream_free(struct slim_stream_runtime*r){assert(!r->active&&!r->prepared);kfree(r);return 0;}
/* FUNCTIONS */
int main(void){struct btfmslim_ch tx[]={{.id=1,.port=1},{.id=0,.port=2},{.id=0,.port=3}},rx[]={{.id=2,.port=4},{.id=3,.port=5}};struct slim_device slave={0};struct device dev={0};struct btfmslim p={.dev=&dev,.slim_pgd=&slave,.bps=16,.num_rx_port=2,.num_tx_port=3,.rx_chs=rx,.tx_chs=tx,.vendor_port_en=vendor};dev.data=&p;struct hwep_data ep={&dev};int cases=0;
for(int f=0;f<7;f++){int expected=-EIO;switch(f){case 0:alloc_fault=alloc_step+1;expected=-ENOMEM;break;case 1:alloc_fault=alloc_step+2;expected=-ENOMEM;break;case 2:vendor_fail=1;break;case 3:fail_prepare=1;break;case 4:fail_enable=1;break;case 5:fail_enable=1;fail_disable=1;break;case 6:fail_prepare=1;fail_disconnect=1;break;}assert(btfm_slim_enable_ch(&p,&tx[1],0,48000,2)==expected);if(f<5)assert(!tx[1].dai.sruntime&&!live&&!ports);else assert(tx[1].dai.sruntime&&live==2);alloc_fault=fail_prepare=fail_enable=fail_disable=fail_disconnect=vendor_fail=0;assert(!btfm_slim_disable_ch(&p,&tx[1],0,2)&&!live&&!ports);assert(!btfm_num_ports_open);cases++;}
assert(btfm_slim_enable_ch(&p,&tx[2],0,48000,2)==-EINVAL);assert(btfm_slim_enable_ch(&p,&tx[0],0,48000,0)==-EINVAL);assert(btfm_slim_enable_ch(&p,&tx[0],0,48000,2)==-EINVAL);tx[0].port=32;assert(btfm_slim_enable_ch(&p,&tx[0],0,48000,1)==-EINVAL);tx[0].port=1;
assert(!btfm_slim_enable_ch(&p,&rx[0],1,44100,1)&&btfm_num_ports_open==1);assert(btfm_slim_enable_ch(&p,&rx[0],1,44100,1)==-EISCONN);vendor_down_fail=1;assert(btfm_slim_disable_ch(&p,&rx[0],1,1)==-EIO&&rx[0].dai.sruntime&&ports);vendor_down_fail=0;assert(!btfm_slim_disable_ch(&p,&rx[0],1,1)&&!ports&&!live&&!btfm_num_ports_open);
unsigned txslots[]={21,22,23},rxslots[]={24,25},out[2],nt,nr;assert(!btfm_slim_dai_set_channel_map(&ep,3,txslots,2,rxslots));assert(btfm_slim_dai_set_channel_map(&ep,4,txslots,2,rxslots)==-EINVAL);assert(btfm_slim_dai_set_channel_map(&ep,1,NULL,0,NULL)==-EINVAL);rxslots[1]=256;assert(btfm_slim_dai_set_channel_map(&ep,3,txslots,2,rxslots)==-EINVAL&&rx[1].ch==25);rxslots[1]=25;
assert(!btfm_slim_dai_get_channel_map(&ep,&nt,out,&nr,NULL,0)&&nt==2&&!nr&&out[0]==22&&out[1]==23);assert(btfm_slim_dai_get_channel_map(&ep,NULL,out,&nr,NULL,0)==-EINVAL);assert(btfm_slim_dai_get_channel_map(&ep,&nt,out,&nr,NULL,2)==-EINVAL);assert(btfm_slim_dai_get_channel_map(&ep,&nt,NULL,&nr,out,5)==-EINVAL);
struct master_hwep_configurations cfg={0};assert(btfm_slim_dai_get_configs(&ep,&cfg,0)==1&&cfg.chan_num==22&&cfg.num_channels==2&&cfg.direction==1);assert(btfm_slim_dai_get_configs(&ep,&cfg,3)==1&&cfg.chan_num==25&&cfg.num_channels==1&&cfg.direction==0);assert(btfm_slim_dai_get_configs(&ep,&cfg,255)==-EINVAL&&btfm_slim_dai_get_configs(&ep,NULL,0)==-EINVAL);
assert(btfm_slim_dai_hw_params(&ep,0,0,1)==-EINVAL&&btfm_slim_dai_hw_params(&ep,16,2,1)==-EINVAL&&btfm_slim_dai_hw_params(&ep,16,0,0)==-EINVAL);assert(!btfm_slim_dai_hw_params(&ep,16,0,1));assert(btfm_slim_dai_prepare(&ep,48000,1,2)==-EINVAL);assert(!btfm_slim_dai_prepare(&ep,48000,0,2));assert(btfm_slim_dai_set_channel_map(&ep,0,NULL,0,NULL)==-EBUSY);assert(!btfm_slim_disable_ch(&p,rx,1,1)&&!live);
p.stopping=true;assert(btfm_slim_dai_get_configs(&ep,&cfg,0)==-ENODEV&&btfm_slim_dai_prepare(&ep,48000,0,2)==-ENODEV&&btfm_slim_dai_hw_params(&ep,16,0,1)==-ENODEV);printf("{\"fault_cases\":%d,\"retryable_partial_cleanup\":true,\"channel_map_and_config_bounds\":true,\"all_four_native_dai_ids\":true,\"allocation_errors_balanced\":true}\n",cases);}
