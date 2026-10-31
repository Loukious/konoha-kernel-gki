#include "btconsumer_mock.h"
struct snd_soc_pcm_stream{int unused;};
struct snd_soc_component{void*data;};
struct snd_ctl_elem_value{int value;};struct snd_ctl_elem_id{char name[64];};
struct snd_kcontrol{void*private_data;struct snd_ctl_elem_id id;};
struct snd_kcontrol_new{const char*name;int(*get)(struct snd_kcontrol*,struct snd_ctl_elem_value*);int(*put)(struct snd_kcontrol*,struct snd_ctl_elem_value*);};
/* HW HEADER */
/* CODEC HEADER */
static struct btfmcodec_data codec;
struct btfmcodec_data*btfm_get_btfmcodec(void){return &codec;}
static int cb_return,cb_calls,probe_return,probe_calls,remove_calls,add_return,add_calls;
static uint32_t bits_per_second=24;static uint8_t num_channels=2;
static int control(struct snd_kcontrol*k,struct snd_ctl_elem_value*v){cb_calls++;v->value=7;return cb_return;}
static void*snd_soc_component_get_drvdata(struct snd_soc_component*c){return c->data;}
int btfmcodec_endpoint_enter(void){return 0;}
void btfmcodec_endpoint_exit(int n){}
static int probe(struct snd_soc_component*c){probe_calls++;return probe_return;}
static void remove_ep(struct snd_soc_component*c){remove_calls++;}
static int btfm_codec_get_mixer_control(struct snd_kcontrol*,struct snd_ctl_elem_value*);
static int btfmcodec_put_mixer_control(struct snd_kcontrol*,struct snd_ctl_elem_value*);
static int snd_soc_add_component_controls(struct snd_soc_component*c,struct snd_kcontrol_new*k,int n){assert(n==2&&k[0].get==btfm_codec_get_mixer_control&&k[0].put==btfmcodec_put_mixer_control&&!k[1].put);add_calls++;return add_return;}
/* FUNCTIONS */
int main(void){struct snd_kcontrol_new controls[]={{"Codec",control,control},{"ReadOnly",control,NULL}};struct hwep_comp_drv drv={.hwep_probe=probe,.hwep_remove=remove_ep};struct hwep_data ep={.drv=&drv,.num_mixer_ctrl=2,.mixer_ctrl=controls};codec.hwep_info=&ep;struct snd_soc_component component={.data=&codec};struct snd_kcontrol k={.private_data=&component,.id={"Codec"}};struct snd_ctl_elem_value v={0};unsigned cases=0;
for(int ret=-32;ret<=1;ret++){cb_return=ret;cb_calls=0;assert(btfm_codec_get_mixer_control(&k,&v)==ret&&v.value==7);assert(btfmcodec_put_mixer_control(&k,&v)==ret&&cb_calls==2);cases++;}strcpy(k.id.name,"ReadOnly");assert(btfmcodec_put_mixer_control(&k,&v)==-EOPNOTSUPP);strcpy(k.id.name,"Missing");assert(btfmcodec_put_mixer_control(&k,&v)==-ENOENT&&btfm_codec_get_mixer_control(&k,&v)==-ENOENT);
probe_return=-ENODEV;assert(btfmcodec_codec_probe(&component)==-ENODEV&&!add_calls&&!remove_calls);probe_return=0;alloc_fault=alloc_step+1;assert(btfmcodec_codec_probe(&component)==-ENOMEM&&remove_calls==1&&!live);alloc_fault=0;add_return=-EIO;assert(btfmcodec_codec_probe(&component)==-EIO&&remove_calls==2&&!live);add_return=0;assert(!btfmcodec_codec_probe(&component)&&!live);codec.states.current_state=BTADV_AUDIO_Connected;btfmcodec_codec_remove(&component);assert(remove_calls==3);ep.num_mixer_ctrl=0;assert(!btfmcodec_codec_probe(&component));btfmcodec_codec_remove(&component);assert(remove_calls==4);
INIT_LIST_HEAD(&codec.config_head);assert(!btfmcodec_is_valid_cache_avb(&codec));assert(btfmcodec_check_and_cache_configs(&codec,48000,1,2,3)==1&&live==1);struct hwep_configurations*p=list_first_entry(&codec.config_head,struct hwep_configurations,dai_list);assert(p->stream_id==2&&p->sample_rate==48000&&p->direction==1&&p->codectype==3&&p->num_channels==2&&p->bit_width==24);alloc_fault=alloc_step+1;assert(btfmcodec_check_and_cache_configs(&codec,96000,0,2,4)==-ENOMEM&&live==1);assert(list_first_entry(&codec.config_head,struct hwep_configurations,dai_list)==p&&p->sample_rate==48000);alloc_fault=0;assert(btfmcodec_check_and_cache_configs(&codec,96000,0,2,4)==1&&live==1);p=list_first_entry(&codec.config_head,struct hwep_configurations,dai_list);assert(p->sample_rate==96000&&p->codectype==4&&btfmcodec_is_valid_cache_avb(&codec));list_del(&p->dai_list);kfree(p);assert(!live&&!btfmcodec_is_valid_cache_avb(&codec));printf("{\"mixer_return_cases\":%u,\"component_failure_unwind\":true,\"remove_in_active_transport\":true,\"cache_replacement_failure_preserves_old\":true}\n",cases);}
