/* SERVICES */
static int phase,cleanups;
static int lpass_cdc_tx_macro_release_clocks(struct lpass_cdc_tx_macro_priv*p){assert(p->hs_closing&&!g_lpass_cdc_tx_priv&&(phase==0||phase==1));cleanups++;return 0;}
static void lpass_cdc_unregister_macro(struct device*d,int id){assert(phase==0&&id==TX_MACRO);phase=1;struct lpass_cdc_tx_macro_priv*p=d->data;if(p->component){lpass_cdc_tx_macro_release_clocks(p);p->component=NULL;}}
static void lpass_cdc_tx_macro_cancel_work(struct lpass_cdc_tx_macro_priv*p){assert(phase==1&&!p->component);phase=2;}
static void lpass_cdc_tx_macro_disable_pm(struct lpass_cdc_tx_macro_priv*p){assert(phase==2&&!p->component&&p->hs_closing&&!g_lpass_cdc_tx_priv);phase=3;}
/* SOURCE */
int main(void){struct platform_device d={0};struct lpass_cdc_tx_macro_priv p;struct snd_soc_component c={0};for(int i=0;i<2;i++){init_tx(&p,&d.dev);phase=cleanups=0;if(i)p.component=&c;g_lpass_cdc_tx_priv=&p;lpass_cdc_tx_macro_remove(&d);assert(phase==3&&cleanups==1&&p.removing&&!g_lpass_cdc_tx_priv);}printf("{\"remove\":2}\n");}
