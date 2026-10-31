#include "slim_mock.h"
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{bool used[256];};struct idr{void*slot[256];};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
#define SNDRV_PCM_STREAM_PLAYBACK 0
#define SNDRV_PCM_STREAM_CAPTURE 1
/* PUBLIC HEADER */
/* PRIVATE HEADER */
/* SEG TABLE */
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define spin_lock(p) ((void)(p))
#define spin_unlock(p) ((void)(p))
#define hweight32(n) __builtin_popcount((unsigned)(n))
#define for_each_set_bit(i,p,n) for((i)=0;(i)<(n);(i)++)if((*(p))&BIT(i))
#define kcalloc(n,z,g) kzalloc((n)*(z),g)
static char*kasprintf(int f,const char*fmt,const char*n){char b[80];snprintf(b,sizeof(b),fmt,n);return mock_name(b);}
static int tx_count,tx_fault,fail_disconnect,hook_ret,hook_calls;
int slim_do_transfer(struct slim_controller*c,struct slim_msg_txn*t){assert(c->stream_lock.held);tx_count++;if(tx_count==tx_fault||(fail_disconnect&&t->mc==SLIM_MSG_MC_DISCONNECT_PORT))return -EIO;return 0;}
static int hook(struct slim_stream_runtime*r){assert(r->dev->ctrl->stream_lock.held);hook_calls++;return hook_ret;}
/* FUNCTIONS */
int main(void){struct slim_framer framer={.superfreq=4000};struct slim_controller ctrl={.a_framer=&framer};struct slim_device dev={.ctrl=&ctrl,.dev={.refs=1}};INIT_LIST_HEAD(&dev.stream_list);unsigned channels[]={21,22,23};struct slim_stream_config config={.rate=48000,.bps=16,.ch_count=3,.chs=channels,.port_mask=BIT(1)|BIT(3)|BIT(5),.direction=0};unsigned cases=0;assert(IS_ERR(slim_stream_allocate(NULL,"x")));for(int fail=1;fail<=2;fail++){alloc_fault=alloc_step+fail;struct slim_stream_runtime*r=slim_stream_allocate(&dev,"x");assert(IS_ERR(r)&&PTR_ERR(r)==-ENOMEM&&!live&&dev.dev.refs==1);alloc_fault=0;}struct slim_stream_runtime*r=slim_stream_allocate(&dev,"test");assert(!IS_ERR(r)&&live==2&&dev.dev.refs==2);for(int scenario=0;scenario<10;scenario++){struct slim_stream_config bad=config;switch(scenario){case 0:bad.rate=0;break;case 1:bad.bps=0;break;case 2:bad.bps=33;break;case 3:bad.port_mask=0;break;case 4:bad.port_mask|=BIT(40);break;case 5:bad.chs=NULL;break;case 6:bad.ch_count=2;break;case 7:bad.direction=-1;break;case 8:bad.direction=2;break;case 9:bad.rate=100;break;}assert(slim_stream_prepare(r,&bad)==-EINVAL&&!r->ports&&live==2&&!tx_count);cases++;}
for(int fail=1;fail<=3;fail++){tx_count=0;tx_fault=fail;assert(slim_stream_prepare(r,&config)==-EIO&&!r->ports&&live==2);cases++;}tx_count=0;tx_fault=2;fail_disconnect=1;assert(slim_stream_prepare(r,&config)==-EIO&&r->ports&&r->ports[0].state==SLIM_PORT_UNCONFIGURED&&live==3);assert(dev.dev.refs==2);fail_disconnect=0;tx_fault=0;assert(!slim_stream_unprepare(r)&&!r->ports&&live==2);
assert(!slim_stream_prepare(r,&config)&&r->num_ports==3&&r->ratem==12);for(int i=0;i<3;i++)assert(r->ports[i].id==1+i*2&&r->ports[i].ch.id==21+i&&r->ports[i].direction==SLIM_PORT_SINK);assert(slim_stream_prepare(r,&config)==-EINVAL);for(int fail=1;fail<=11;fail++){tx_count=0;tx_fault=fail;assert(slim_stream_enable(r)==-EIO);cases++;}tx_fault=0;assert(!slim_stream_enable(r));for(int i=0;i<3;i++)assert(r->ports[i].ch.state==SLIM_CH_STATE_ACTIVE&&r->ports[i].state==SLIM_PORT_CONFIGURED);ctrl.enable_stream=hook;hook_ret=-EAGAIN;assert(slim_stream_enable(r)==-EAGAIN&&hook_calls==1);hook_ret=0;assert(!slim_stream_enable(r));ctrl.enable_stream=NULL;ctrl.disable_stream=hook;hook_ret=-EIO;assert(slim_stream_disable(r)==-EIO);ctrl.disable_stream=NULL;assert(!slim_stream_disable(r));assert(slim_stream_unprepare_disconnect_port(r,false,true)==-EBUSY);tx_count=0;tx_fault=2;assert(slim_stream_unprepare(r)==-EIO&&r->ports[0].state==SLIM_PORT_DISCONNECTED&&r->ports[1].state!=SLIM_PORT_DISCONNECTED);tx_fault=0;assert(!slim_stream_unprepare(r)&&!r->ports);assert(!slim_stream_free(r)&&!live&&dev.dev.refs==1&&list_empty(&dev.stream_list));r=slim_stream_allocate(&dev,"remove");assert(!slim_stream_prepare(r,&config));ctrl.removing=true;assert(slim_stream_enable(r)==-ESHUTDOWN&&slim_stream_prepare(r,&config)==-ESHUTDOWN);assert(!slim_stream_free(r)&&!live&&dev.dev.refs==1);assert(PTR_ERR(slim_stream_allocate(&dev,"late"))==-ESHUTDOWN);printf("{\"stream_input_and_failure_cases\":%u,\"rollback_retry_ledger\":true,\"transaction_errors_propagate\":true,\"serialized_hooks\":true,\"stream_retains_device\":true,\"removal_releases_software_state\":true}\n",cases);}
