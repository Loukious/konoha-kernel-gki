/* SERVICES */
/* CONSTANTS */
#define LPASS_CDC_VERSION_1_0 1
#define LPASS_CDC_VERSION_1_1 2
#define LPASS_CDC_VERSION_1_2 3
#define LPASS_CDC_VERSION_2_0 4
#define LPASS_CDC_VERSION_2_1 5
#define LPASS_CDC_VERSION_2_5 6
#define LPASS_CDC_VERSION_2_6 7
#define SND_EVENT_UP 1
static int lpass_cdc_ssr_ops;static int snd_live,typec_live,init_count,exit_count,exits[5],next_macro;static bool typec_gate;
static void snd_soc_component_init_regmap(struct snd_soc_component*c,struct regmap*m){}
static void snd_soc_component_exit_regmap(struct snd_soc_component*c){assert(!snd_live&&!typec_live);}
static unsigned int snd_soc_component_read(struct snd_soc_component*c,int r){return 0;}
static int snd_event_client_register(struct device*d,void*ops,void*data){if(fail())return -EIO;snd_live=1;return 0;}
static int snd_event_notify(struct device*d,int event){return fail()?-EIO:0;}
static void snd_event_client_deregister(struct device*d){assert(snd_live);snd_live=0;}
static int typec_analog_acc_init(struct snd_soc_component*c){if(fail())return -EIO;typec_live=1;return 0;}
static void typec_analog_acc_exit(struct snd_soc_component*c){assert(typec_live);typec_live=0;}
static void snd_info_free_entry(struct snd_info_entry*e){kfree(e);}
static int macro_init(struct snd_soc_component*c){if(fail())return -EIO;init_count++;return 0;}
static int e0(struct snd_soc_component*c){assert(!snd_live&&!typec_live);exits[exit_count++]=0;return 0;}
static int e1(struct snd_soc_component*c){assert(!snd_live&&!typec_live);exits[exit_count++]=1;return 0;}
static int e2(struct snd_soc_component*c){assert(!snd_live&&!typec_live);exits[exit_count++]=2;return 0;}
#define of_find_property(n,p,l) ((n)->split?(n):NULL)
/* SOURCE */
int main(void){struct device_node n={.split=true};struct device d={.of_node=&n};struct snd_soc_card card={.dev=&d};struct snd_soc_component c={.dev=&d,.card=&card};struct lpass_cdc_priv p;int cases=0;
for(int f=1;f<=6;f++){init_priv(&p,&d);p.num_macros_registered=3;for(int i=0;i<3;i++)p.macro_params[i].init=macro_init;p.macro_params[0].exit=e0;p.macro_params[1].exit=e1;p.macro_params[2].exit=e2;steps=0;fault=f;init_count=exit_count=0;assert(lpass_cdc_soc_codec_probe(&c)==-EIO);assert(init_count==exit_count&&!p.component&&!p.component_ready&&!snd_live&&!typec_live);for(int i=0;i<exit_count;i++)assert(exits[i]==exit_count-i-1);cases++;}
init_priv(&p,&d);p.num_macros_registered=3;for(int i=0;i<3;i++)p.macro_params[i].init=macro_init;p.macro_params[0].exit=e0;p.macro_params[1].exit=e1;p.macro_params[2].exit=e2;fault=0;init_count=exit_count=0;assert(!lpass_cdc_soc_codec_probe(&c));assert(p.component_ready&&snd_live&&typec_live);lpass_cdc_soc_codec_remove(&c);assert(!p.component&&!snd_live&&!typec_live&&exit_count==3&&exits[0]==2&&exits[2]==0);cases+=2;printf("{\"component\":%d}\n",cases);}
