/* SERVICES */
static struct snd_soc_dapm_context dapm;
static int lpass_cdc_tx_macro_dapm_widgets[1],tx_audio_map[1],lpass_cdc_tx_macro_snd_controls[1];
static struct snd_soc_dapm_context *snd_soc_component_get_dapm(struct snd_soc_component*c){return &dapm;}
static int lpass_cdc_get_version(struct device*d){return 7;}
static int snd_soc_dapm_new_controls(void*d,void*w,int n){struct lpass_cdc_tx_macro_priv*p=macro_dev->data;assert(p->component);return fail()?-EIO:0;}
static int snd_soc_dapm_add_routes(void*d,void*w,int n){return fail()?-EIO:0;}
static int snd_soc_dapm_new_widgets(void*d){return fail()?-EIO:0;}
static int snd_soc_add_component_controls(void*d,void*w,int n){return fail()?-EIO:0;}
static void snd_soc_dapm_ignore_suspend(void*d,const char*n){}
static void snd_soc_dapm_sync(void*d){}
static int releases;
static int lpass_cdc_tx_macro_component_cleanup(struct lpass_cdc_tx_macro_priv*p){assert(p->component&&!p->wlock_holders);return 0;}
static int lpass_cdc_tx_macro_release_clocks(struct lpass_cdc_tx_macro_priv*p){assert(p->component&&!p->wlock_holders);releases++;return 0;}
/* SOURCE */
int main(void){struct device parent={0},d={.parent=&parent};macro_dev=&d;static struct regmap map;parent.regmap=&map;struct lpass_cdc_tx_macro_priv p;struct snd_soc_component c={.dev=&parent};int cases=0;
for(int f=1;f<=5;f++){init_tx(&p,&d);steps=0;fault=f;assert(lpass_cdc_tx_macro_init(&c)==-EIO&&!p.component&&!p.wlock_holders&&!awake);cases++;}
fault=0;init_tx(&p,&d);assert(!lpass_cdc_tx_macro_init(&c)&&p.component==&c);lpass_cdc_tx_macro_queue_work(&p,&p.tx_hpf_work[0].dwork,4);lpass_cdc_tx_macro_queue_work(&p,&p.tx_mute_dwork[1].dwork,4);p.tx_dec_unmute_work[1].dwork.pending=true;p.removing=true;assert(!lpass_cdc_tx_macro_deinit(&c));assert(!p.component&&!p.wlock_holders&&!awake&&!p.tx_dec_unmute_work[1].dwork.pending&&releases==1);cases+=2;printf("{\"component\":%d}\n",cases);}
