/* SERVICES */
static int va_tx_unmute_delay=40;
/* SOURCE */
static void setup(struct lpass_cdc_va_macro_priv*p,struct device*d,struct snd_soc_component*c){init_va(p,d);p->component=c;for(int i=0;i<4;i++){p->va_hpf_work[i].va_priv=p;p->va_hpf_work[i].decimator=i;p->va_mute_dwork[i].va_priv=p;p->va_mute_dwork[i].decimator=i;}d->parent->regmap->values[LPASS_CDC_VA_INP_MUX_ADC_MUX0_CFG0]=16;}
int main(void){struct device parent={0},d={.parent=&parent};macro_dev=&d;static struct regmap map;parent.regmap=&map;struct lpass_cdc_va_macro_priv p;struct snd_soc_component c={.dev=&parent};struct snd_soc_dapm_context dapm={.component=&c};struct snd_soc_dapm_widget w={.dapm=&dapm,.shift=0,.name="DEC0"};int cases=0;
for(int f=1;f<=4;f++){setup(&p,&d,&c);steps=0;fault=f;assert(lpass_cdc_va_macro_enable_dec(&w,NULL,1)==-EIO);assert(!p.dmic_owned[0]&&!dmic_refs[0]&&!awake);cases++;}
fault=0;setup(&p,&d,&c);assert(!lpass_cdc_va_macro_enable_dec(&w,NULL,1));int before=steps;assert(!lpass_cdc_va_macro_enable_dec(&w,NULL,2));int writes=steps-before;assert(writes==7&&p.dmic_owned[0]&&p.wlock_holders==2);lpass_cdc_va_macro_cancel_work(&p);assert(!lpass_cdc_va_macro_enable_dec(&w,NULL,8)&&!p.dmic_owned[0]);cases++;
for(int f=1;f<=writes;f++){fault=0;memset(&map,0,sizeof(map));setup(&p,&d,&c);assert(!lpass_cdc_va_macro_enable_dec(&w,NULL,1));steps=0;fault=f;assert(lpass_cdc_va_macro_enable_dec(&w,NULL,2)==-EIO);assert(!p.dmic_owned[0]&&!dmic_refs[0]&&!awake&&!p.wlock_holders);cases++;}
fault=0;setup(&p,&d,&c);assert(!lpass_cdc_va_macro_enable_dec(&w,NULL,1));map.values[LPASS_CDC_VA_INP_MUX_ADC_MUX0_CFG0]=32;assert(!lpass_cdc_va_macro_enable_dec(&w,NULL,8));assert(!dmic_refs[0]&&!dmic_refs[1]);cases++;
for(int f=1;f<=3;f++){setup(&p,&d,&c);assert(!lpass_cdc_va_macro_wake_enable(&p,true));steps=0;fault=f;errors=0;lpass_cdc_va_macro_tx_hpf_corner_freq_callback(&p.va_hpf_work[0].dwork.work);assert(errors==1&&!awake&&!p.wlock_holders);cases++;}
setup(&p,&d,&c);assert(!lpass_cdc_va_macro_wake_enable(&p,true));steps=0;fault=1;errors=0;lpass_cdc_va_macro_mute_update_callback(&p.va_mute_dwork[0].dwork.work);assert(errors==1&&!awake&&!p.wlock_holders);cases++;printf("{\"dec\":%d}\n",cases);}
