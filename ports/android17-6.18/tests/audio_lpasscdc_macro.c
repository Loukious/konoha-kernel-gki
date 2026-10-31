/* SERVICES */
static const struct snd_soc_component_driver lpass_cdc;
static int registered,unregistered;
static int snd_soc_register_component(struct device*d,const struct snd_soc_component_driver*c,struct snd_soc_dai_driver*dai,int n){if(fail())return -EIO;assert(n==3&&dai[0].id==1&&dai[1].id==2&&dai[2].id==3);registered++;return 0;}
static void snd_soc_unregister_component(struct device*d){struct lpass_cdc_priv*p=d->data;assert(p->macro_params[0].dev&&p->macro_params[1].dev);assert(p->num_dais==3);unregistered++;registered--;}
/* SOURCE */
int main(void){int cases=0;struct device_node node={.compatible="qcom,lpass-cdc"};struct device d={.of_node=&node},a={.parent=&d},b={.parent=&d};struct lpass_cdc_priv p;struct snd_soc_dai_driver dais[]={{1},{2},{3}};struct macro_ops x={.io_base=(void*)1,.num_dais=1,.dai_ptr=dais},y={.io_base=(void*)2,.num_dais=2,.dai_ptr=dais+1};
for(int f=1;f<=2;f++){init_priv(&p,&d);p.num_macros=2;fault=0;assert(!lpass_cdc_register_macro(&a,0,&x));steps=0;fault=f;assert(lpass_cdc_register_macro(&b,1,&y)<0);assert(p.num_dais==1&&p.num_macros_registered==1&&!p.macros_supported[1]&&!p.macro_params[1].dev&&!p.lpass_cdc_dais);lpass_cdc_unregister_macro(&a,0);assert(!p.num_dais&&!p.num_macros_registered&&!live);cases++;}
fault=0;init_priv(&p,&d);p.num_macros=2;assert(!lpass_cdc_register_macro(&a,0,&x));assert(lpass_cdc_register_macro(&a,0,&x)==-EBUSY);assert(!lpass_cdc_register_macro(&b,1,&y));assert(registered==1&&p.num_dais==3);lpass_cdc_unregister_macro(&a,0);assert(unregistered==1&&!registered&&p.num_dais==2&&p.num_macros_registered==1&&!p.macros_supported[0]&&!p.lpass_cdc_dais);assert(!lpass_cdc_register_macro(&a,0,&x));assert(registered==1&&p.num_dais==3);lpass_cdc_unregister_macro(&b,1);assert(unregistered==2&&p.num_dais==1);lpass_cdc_unregister_macro(&a,0);assert(!p.num_dais&&!live);cases+=6;
printf("{\"macro\":%d}\n",cases);}
