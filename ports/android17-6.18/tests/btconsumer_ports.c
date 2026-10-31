#include "btconsumer_mock.h"
#undef BTFMSWR_ERR
#undef BTFMSWR_INFO
#undef BTFMSWR_DBG
struct swr_device{struct device dev;u8 dev_num;};
/* SWR HEADER */
/* SLAVE HEADER */
/* SLAVE TABLE */
static struct swr_device slave;
struct btfmswr context,*pbtfmswr=&context;
static DEFINE_MUTEX(btfm_swr_lock);
static int btfm_num_ports_open;
static int connect_ret,disconnect_ret,data_ret,connect_calls,disconnect_calls,data_calls,reset_ret,reset_calls,logical_fail,logical_calls;
static int chipset=QCA_GANGES_SOC_ID_0100;
#define SWRS_SCP_CONTROL 0x44
#define DELAY_BEFORE_SLAVE_RESET 10
#define DELAY_FOR_SWR_SLAVE_RESET 20
#define msleep(n) ((void)0)
#define usleep_range(a,b) ((void)0)
static int btpower_get_chipset_version(void){return chipset;}
static int swr_get_logical_dev_num(struct swr_device*s,u64 ea,u8*num){logical_calls++;assert(ea==GANGES_EA||ea==EVROS_EA);if(logical_fail)return -EIO;*num=9;return 0;}
static int swr_write(struct swr_device*s,u8 num,unsigned reg,u8*value){assert(num==9&&reg==0x44&&*value==0x80);reset_calls++;return reset_ret;}
static int swr_connect_port(struct swr_device*s,u8*p,int n,u8*mask,u32*rate,u8*channels,u8*usecase){assert(btfm_swr_lock.held&&s==&slave&&n==1&&*p<5&&*mask==(*channels==2?3:1)&&*rate==48000&&*usecase==7);connect_calls++;return connect_ret;}
static int swr_disconnect_port(struct swr_device*s,u8*p,int n,u8*mask,u8*usecase){assert(btfm_swr_lock.held&&s==&slave&&n==1&&*p<5&&(*mask==1||*mask==3)&&*usecase==7);disconnect_calls++;return disconnect_ret;}
static int swr_slvdev_datapath_control(struct swr_device*s,u8 num,bool on){assert(btfm_swr_lock.held&&s==&slave&&num==9);data_calls++;return data_ret;}
/* FUNCTIONS */
static void reset(void){memset(&context,0,sizeof(context));context.swr_slave=&slave;context.initialized=true;slave.dev_num=9;pbtfmswr=&context;btfm_num_ports_open=0;connect_ret=disconnect_ret=data_ret=0;connect_calls=disconnect_calls=data_calls=0;}
int main(void){unsigned cases=0;reset();unsigned g[]={QCA_GANGES_SOC_ID_0100,QCA_GANGES_SOC_ID_0200,QCA_ORNE_SOC_ID_0100,QCA_COLOGNE_SOC_ID_0100,0};for(unsigned i=0;i<ARRAY_SIZE(g);i++)assert(btfm_get_bt_soc_index(g[i])==GANGES);for(unsigned id=0x40200100;id<=0x40200105;id++)assert(btfm_get_bt_soc_index(id)==EVROS);assert(btfm_get_bt_soc_index(QCA_EVROS_SOC_ID_0200)==EVROS);int ports[][5]={{5,3,1,2,4},{255,4,1,2,5}};for(int soc=0;soc<2;soc++)for(int i=0;i<5;i++)assert(slave_port[soc].port_info[i].port==ports[soc][i]);assert(slave_port[GANGES].ea==0x0208170220ULL&&slave_port[EVROS].ea==0x0108170220ULL);
for(unsigned port=0;port<256;port++)for(unsigned channels=0;channels<4;channels++){reset();int ret=btfm_swr_enable_port(port,channels,48000,7);bool valid=port>=1&&port<=5&&channels>=1&&channels<=2;assert(ret==(valid?0:-EINVAL));if(valid){assert(btfm_num_ports_open==1&&context.ports[port].active);assert(!btfm_swr_disable_port(port,channels,7)&&!btfm_num_ports_open);}else assert(!connect_calls);cases++;}
for(int port=1;port<=5;port++){reset();connect_ret=-EIO;assert(btfm_swr_enable_port(port,2,48000,7)==-EIO&&!btfm_num_ports_open&&!data_calls);connect_ret=0;data_ret=-ETIMEDOUT;assert(btfm_swr_enable_port(port,2,48000,7)==-ETIMEDOUT&&!btfm_num_ports_open&&!context.ports[port].connected);disconnect_ret=-EAGAIN;assert(btfm_swr_enable_port(port,2,48000,7)==-ETIMEDOUT&&btfm_num_ports_open==1&&context.ports[port].connected&&!context.ports[port].active);int before=connect_calls;assert(btfm_swr_enable_port(port,1,48000,7)==-EINVAL);assert(btfm_swr_enable_port(port,2,96000,7)==-EINVAL);data_ret=0;assert(!btfm_swr_enable_port(port,2,48000,7)&&connect_calls==before&&btfm_num_ports_open==1);assert(btfm_swr_enable_port(port,2,48000,7)==-EISCONN);assert(btfm_swr_enable_port(port,2,96000,7)==-EINVAL);assert(btfm_swr_disable_port(port,2,7)==-EAGAIN&&context.ports[port].connected&&btfm_num_ports_open==1);disconnect_ret=0;data_ret=-EIO;assert(btfm_swr_disable_port(port,2,7)==-EIO&&!context.ports[port].connected&&context.ports[port].disconnect_pending&&btfm_num_ports_open==1);assert(btfm_swr_enable_port(port,2,48000,7)==-EBUSY);before=disconnect_calls;data_ret=0;assert(!btfm_swr_disable_port(port,2,7)&&disconnect_calls==before&&!btfm_num_ports_open&&!context.ports[port].active);assert(btfm_swr_disable_port(port,2,7)==-ENOENT);}
reset();context.initialized=false;assert(btfm_swr_enable_port(1,1,48000,7)==-EINVAL);reset_ret=-EIO;assert(btfm_swr_hw_init()==-EIO&&!context.initialized);reset_ret=0;logical_fail=1;logical_calls=0;assert(btfm_swr_hw_init()==-EIO&&logical_calls==MAX_GET_DEV_NUM_RETRY&&!context.initialized);logical_fail=0;assert(!btfm_swr_hw_init()&&context.initialized&&slave.dev_num==9);assert(!btfm_swr_enable_port(1,1,48000,7));int before=reset_calls;assert(!btfm_swr_hw_init()&&reset_calls==before);context.stopping=true;assert(btfm_swr_hw_init()==-ENODEV&&btfm_swr_enable_port(1,1,48000,7)==-ENODEV&&btfm_swr_disable_port(1,1,7)==-ENODEV);pbtfmswr=NULL;assert(btfm_swr_hw_init()==-ENODEV&&!btfm_swr_lock.held);printf("{\"port_validation_cases\":%u,\"rollback_and_retry_all_ports\":true,\"sample_rate_retained_on_failed_rollback\":true,\"native_chip_port_tables\":true,\"reset_failures_and_active_port_protection\":true}\n",cases);}
