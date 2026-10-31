#define IPA_REAL_MUTEX
#define BT_REAL_SRCU
#include "btconsumer_mock.h"
struct snd_soc_pcm_stream{unsigned rate;};struct snd_soc_component;
struct snd_kcontrol_new{int unused;};
/* HW HEADER */
/* CODEC HEADER */
/* PACKET HEADER */
struct snd_soc_dai_driver{const char*name;unsigned id;struct snd_soc_pcm_stream capture,playback;const void*ops;};
static struct snd_soc_dai_driver*btfmcodec_dai_info;
static int btfmcodec_dai_ops,btfmcodec_codec_component;
static struct btfmcodec_data context,*btfmcodec=&context;
static struct btfmcodec_char_device channel;
static DEFINE_MUTEX(btfmcodec_registration_lock);
struct srcu_struct btfmcodec_endpoint_srcu={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,false};
static int ssr_error,soc_error,ssr_live,soc_live,drained;
static bool expect_drain;
static int btfmcodec_adsp_ssr_notify(struct notifier_block*n,unsigned long a,void*d){return 0;}
static void*qcom_register_ssr_notifier(const char*n,struct notifier_block*b){if(ssr_error)return ERR_PTR(ssr_error);ssr_live++;return b;}
static void qcom_unregister_ssr_notifier(void*p,struct notifier_block*b){assert(p==b&&ssr_live==1);ssr_live--;}
static int snd_soc_register_component(struct device*d,void*c,struct snd_soc_dai_driver*da,int n){assert(d==&context.dev&&n==1&&da->id==2&&da->capture.rate==48000&&da->ops==&btfmcodec_dai_ops);if(soc_error)return soc_error;soc_live++;return 0;}
static void snd_soc_unregister_component(struct device*d){assert(!btfmcodec_endpoint_srcu.readers&&soc_live==1);if(expect_drain)assert(drained);soc_live--;}
bool btfmcodec_cp_supported(void){return true;}
void btfmcodec_drain_commands(struct btfmcodec_data*c){assert(!btfmcodec_endpoint_srcu.readers);drained++;}
static void btfmcodec_set_current_state(struct btfmcodec_state_machine*s,btfmcodec_state v){s->current_state=v;}
int btfm_register_codec(struct hwep_data*e);
void btfm_unregister_codec(void);
/* FUNCTIONS */
static void*unregister_thread(void*p){assert(!btfmcodec_unregister_hw_ep("btfmswr_slave"));return NULL;}
int main(void){mutex_init(&context.hwep_drv_lock);mutex_init(&context.config_lock);mutex_init(&channel.read_lock);context.btfmcodec_dev=&channel;channel.btfmcodec=&context;channel.ready=true;INIT_LIST_HEAD(&context.config_head);struct device provider={.refs=1};struct hwep_dai_ops ops={0};struct hwep_dai_driver dai={.dai_name="Native",.id=2,.capture={48000},.dai_ops=&ops};struct hwep_data ep={.dev=&provider,.driver_name="btfmswr_slave",.dai_drv=&dai,.num_dai=1};
channel.ready=false;assert(btfmcodec_register_hw_ep(&ep)==-EPROBE_DEFER);channel.ready=true;assert(btfmcodec_register_hw_ep(NULL)==-EINVAL);ep.num_dai=65;assert(btfmcodec_register_hw_ep(&ep)==-EINVAL);ep.num_dai=1;ep.num_mixer_ctrl=1;assert(btfmcodec_register_hw_ep(&ep)==-EINVAL);ep.num_mixer_ctrl=0;memset(ep.driver_name,'x',64);assert(btfmcodec_register_hw_ep(&ep)==-EINVAL);strcpy(ep.driver_name,"btfmswr_slave");assert(btfmcodec_endpoint_enter()==-ENODEV);
unsigned failures=0;for(int scenario=0;scenario<4;scenario++){alloc_fault=0;ssr_error=soc_error=0;alloc_step=0;if(scenario==0)alloc_fault=1;if(scenario==1)ssr_error=-EIO;if(scenario==2)alloc_fault=2;if(scenario==3)soc_error=-ENODEV;assert(btfmcodec_register_hw_ep(&ep)<0&&!context.hwep_info&&provider.refs==1&&!live&&!ssr_live&&!soc_live);assert(btfmcodec_endpoint_enter()==-ENODEV);failures++;}alloc_fault=0;soc_error=0;
assert(!btfmcodec_register_hw_ep(&ep)&&context.hwep_info!=&ep&&provider.refs==2&&soc_live==1&&ssr_live==1&&live==2);assert(test_bit(BTADV_CONFIGURE_DMA,&context.hwep_info->flags));assert(btfmcodec_register_hw_ep(&ep)==-EBUSY);assert(btfmcodec_unregister_hw_ep("wrong")==-ENOENT&&provider.refs==2);struct hwep_configurations*config=kzalloc(sizeof(*config),0);list_add(&config->dai_list,&context.config_head);
int idx=btfmcodec_endpoint_enter();assert(idx>=0);pthread_t thread;expect_drain=true;assert(!pthread_create(&thread,NULL,unregister_thread,NULL));pthread_mutex_lock(&btfmcodec_endpoint_srcu.lock);while(!btfmcodec_endpoint_srcu.synchronizing)pthread_cond_wait(&btfmcodec_endpoint_srcu.changed,&btfmcodec_endpoint_srcu.lock);pthread_mutex_unlock(&btfmcodec_endpoint_srcu.lock);assert(btfmcodec_endpoint_enter()==-ENODEV);assert(provider.refs==2&&context.hwep_info&&!drained&&live==3);btfmcodec_endpoint_exit(idx);assert(!pthread_join(thread,NULL));assert(!context.hwep_info&&provider.refs==1&&!ssr_live&&!soc_live&&!live&&drained==1&&list_empty(&context.config_head));assert(btfmcodec_unregister_hw_ep("btfmswr_slave")==-ENOENT);assert(!btfmcodec_register_hw_ep(&ep));assert(!btfmcodec_unregister_hw_ep("btfmswr_slave")&&!live&&provider.refs==1);printf("{\"registration_failure_steps\":%u,\"native_dai_flags_preserved\":true,\"pthread_callback_blocks_unregister\":true,\"stop_denies_new_callbacks\":true,\"drain_before_provider_release\":true,\"reregister_after_cleanup\":true}\n",failures);}
