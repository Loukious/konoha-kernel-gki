#include "btconsumer_mock.h"
#undef BTFMSWR_ERR
#undef BTFMSWR_INFO
#undef BTFMSWR_DBG
struct snd_soc_pcm_stream{int unused;};struct snd_soc_component;struct snd_kcontrol_new;
struct snd_kcontrol{int unused;};struct snd_ctl_elem_value{struct{struct{long value[1];}integer;}value;};
/* SWR HEADER */
/* HW HEADER */
#define SOC_ENUM_SINGLE_EXT_DECL(a,b) int a
/* SWRIF HEADER */
static u8 usecase_codec;static int btfm_feedback_ch_setting;
#define SNDRV_PCM_STREAM_CAPTURE 1
#define LPAIF_AUD 5
#define dev_get_drvdata(d) ((d)->data)
/* FUNCTIONS */
int main(void){struct btfmswr priv={.sample_rate=96000};struct device dev={.data=&priv};struct hwep_data ep={.dev=&dev};unsigned cases=0;unsigned rates[]={8000,16000,44100,48000,88200,96000,192000};for(unsigned codec=0;codec<256;codec++)for(unsigned r=0;r<ARRAY_SIZE(rates);r++){usecase_codec=codec;u32 actual=rates[r],expected=actual;if(codec==LDAC||codec==APTX_AD||codec==LHDC){if(actual==44100||actual==48000)expected*=2;}if(codec==LC3_VOICE||codec==APTX_AD_SPEECH||codec==LC3||codec==APTX_AD_R4||codec==RVP)expected=96000;if(codec==APTX_AD_QLEA)expected=192000;btfm_get_sampling_rate(&actual);assert(actual==expected);cases++;}
struct snd_ctl_elem_value value={0};for(long v=-1;v<=256;v++){value.value.integer.value[0]=v;assert(btfm_put_codec_type(NULL,&value)==(v>=0&&v<16?1:-EINVAL));assert(btfm_put_feedback_ch_setting(NULL,&value)==(v>=0&&v<=1?1:-EINVAL));}usecase_codec=LC3;
for(unsigned width=0;width<=33;width++)for(unsigned direction=0;direction<=2;direction++)for(unsigned channels=0;channels<=3;channels++){bool valid=width>=1&&width<=32&&direction<=1&&channels>=1&&channels<=2;assert(btfm_swr_dai_hw_params(&ep,width,direction,channels)==(valid?0:-EINVAL));if(valid)assert(priv.bps==width&&priv.num_channels==channels&&priv.direction==direction);}
assert(!btfm_swr_dai_hw_params(&ep,24,1,2));struct hwep_dma_configurations config;unsigned tx,rx,ts,rs;for(unsigned id=0;id<256;id++){int ret=btfm_swr_dai_get_configs(&ep,&config,id);assert(ret==(id<5?1:-EINVAL));if(id<5)assert(config.stream_id==id&&config.sample_rate==96000&&config.bit_width==24&&config.num_channels==2&&config.codectype==LC3&&config.active_channel_mask==3&&config.lpaif==5&&config.inf_index==(id==3||id==4?2:1));ret=btfm_swr_dai_get_channel_map(&ep,&tx,&ts,&rx,&rs,id);assert(ret==(id<5?0:-EINVAL));if(id<5){bool capture=id==0||id==1||id==4;assert(tx==(capture?2:0)&&ts==(capture?3:0)&&rx==(capture?0:2)&&rs==(capture?0:3));}}assert(btfm_swr_dai_get_configs(&ep,NULL,2)==-EINVAL);assert(btfm_swr_dai_get_channel_map(&ep,NULL,&ts,&rx,&rs,2)==-EINVAL);printf("{\"codec_rate_cases\":%u,\"mixer_bounds\":true,\"parameter_validation\":true,\"native_dma_fields_and_positive_return\":true,\"channel_map_bounds\":true}\n",cases);}
