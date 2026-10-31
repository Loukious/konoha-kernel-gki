#include "btconsumer_mock.h"
struct snd_soc_pcm_stream{int unused;};
struct snd_soc_component;struct snd_kcontrol_new;
/* HW HEADER */
/* CODEC HEADER */
/* PACKET HEADER */
static struct btfmcodec_data codec;
static struct btfmcodec_char_device channel;
static int get_ret=1,enqueue_ret,enqueue_calls,wait_calls,wait_ret=1,response=BTM_RSP_RECV,shutdowns,stop_in_wait;
static bool wait_condition;
static unsigned request_len;static u8 request[64];
static int get_config(void*a,void*b,u8 id){if(get_ret<0)return get_ret;struct hwep_data*e=a;if(e->flags==1){struct master_hwep_configurations c={.stream_id=id,.device_id=0x12345678,.sample_rate=48000,.bit_width=24,.num_channels=2,.chan_num=7,.codectype=3};memcpy(b,&c,sizeof(c));}else{struct hwep_dma_configurations c={.stream_id=id,.sample_rate=96000,.bit_width=16,.num_channels=2,.codectype=4,.lpaif=5,.inf_index=6,.active_channel_mask=3};memcpy(b,&c,sizeof(c));}return get_ret;}
static void shutdown_ep(void*a,int id){assert(id==2);shutdowns++;}
static btfmcodec_state btfmcodec_get_current_transport(struct btfmcodec_state_machine*s){return s->current_state;}
int btfmcodec_dev_enqueue_pkt(struct btfmcodec_char_device*c,void*p,int len){enqueue_calls++;if(enqueue_ret)return enqueue_ret;assert(len<=sizeof(request));memcpy(request,p,len);request_len=len;return 0;}
static void before_wait(void){wait_calls++;if(stop_in_wait==1)codec.hwep_stopping=true;if(stop_in_wait==2)channel.closing=true;if(response>=0)for(unsigned i=0;i<BTM_PKT_TYPE_MAX;i++)channel.status[i]=response;}
#define msecs_to_jiffies(n) (n)
#define wait_event_interruptible_timeout(q,c,t) ({before_wait();wait_condition=(c);wait_ret;})
/* FUNCTIONS */
int main(void){struct hwep_dai_ops ops={.hwep_get_configs=get_config,.hwep_shutdown=shutdown_ep};struct hwep_dai_driver dai={.dai_ops=&ops};struct hwep_data ep={.dai_drv=&dai};codec.hwep_info=&ep;codec.btfmcodec_dev=&channel;channel.btfmcodec=&codec;codec.states.current_state=BT_Connected;unsigned cases=0;
for(int kind=0;kind<4;kind++)for(int scenario=0;scenario<10;scenario++){ep.flags=kind==0?1:0;get_ret=1;enqueue_ret=0;wait_ret=1;response=BTM_RSP_RECV;stop_in_wait=0;codec.hwep_stopping=false;channel.closing=false;enqueue_calls=wait_calls=shutdowns=0;int expected=0;
switch(scenario){case 1:enqueue_ret=-ENOSPC;expected=-ENOSPC;break;case 2:wait_ret=0;response=-1;expected=-ETIMEDOUT;break;case 3:wait_ret=-ERESTARTSYS;expected=-ERESTARTSYS;break;case 4:response=BTM_FAIL_RESP_RECV;expected=-EIO;break;case 5:response=BTM_RSP_NOT_RECV_CLIENT_KILLED;expected=-EIO;break;case 6:stop_in_wait=1;response=-1;expected=-ESHUTDOWN;break;case 7:stop_in_wait=2;response=-1;expected=-ESHUTDOWN;break;case 8:if(kind<2){get_ret=-EIO;expected=-EIO;}break;case 9:get_ret=0;break;}
int ret=kind==0?btfmcodec_configure_master(&codec,2):kind==1?btfmcodec_configure_dma(&codec,2):kind==2?btfmcodec_hwep_shutdown(&codec,2,true):btfmcodec_notify_usecase_start(&codec,2,2);assert(ret==expected);assert(shutdowns==(kind==2));if(enqueue_ret || get_ret<0)assert(!wait_calls);if(stop_in_wait)assert(wait_condition);if(!expected){assert(enqueue_calls==1);assert(get_unaligned_le32(request+4)+8==request_len);if(kind==0){assert(request_len==sizeof(struct btm_master_config_req));struct btm_master_config_req*p=(void*)request;assert(p->opcode==BTM_BTFMCODEC_MASTER_CONFIG_REQ&&p->stream_id==2&&p->device_id==0x12345678&&p->sample_rate==48000&&p->bit_width==24&&p->num_channels==2&&p->channel_num==7&&p->codec_id==3);}if(kind==1){assert(request_len==sizeof(struct btm_dma_config_req));struct btm_dma_config_req*p=(void*)request;assert(p->opcode==BTM_BTFMCODEC_CODEC_CONFIG_DMA_REQ&&p->stream_id==2&&p->sample_rate==96000&&p->bit_width==16&&p->num_channels==2&&p->codec_id==4&&p->lpaif==5&&p->inf_index==6&&p->active_channel_mask==3);}}cases++;}
codec.hwep_stopping=false;channel.closing=false;shutdowns=enqueue_calls=0;assert(!btfmcodec_hwep_shutdown(&codec,2,false)&&shutdowns==1&&!enqueue_calls);printf("{\"command_failure_and_framing_cases\":%u,\"positive_native_get_configs\":true,\"stop_after_status_reset\":true,\"shutdown_hardware_cleanup_on_failure\":true}\n",cases);}
