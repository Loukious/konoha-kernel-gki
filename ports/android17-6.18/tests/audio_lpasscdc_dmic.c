/* SERVICES */
/* CONSTANTS */
/* SOURCE */
static int divisor(struct snd_soc_component*c,u32 pair){return 2;}
int main(void){struct lpass_cdc_priv p;struct device d={0};struct snd_soc_component c={.dev=&d};static struct regmap map;int cases=0;
for(int f=1;f<=5;f++){init_priv(&p,&d);memset(&map,0,sizeof(map));p.regmap=&map;p.macro_params[0].clk_div_get=divisor;fault=f;steps=0;assert(lpass_cdc_dmic_clk_enable_active(&c,0,0,true)==-EIO);assert(!p.dmic_0_1_tx_clk_cnt&&!p.dmic_0_1_clk_div&&map.values[LPASS_CDC_VA_TOP_CSR_DMIC0_CTL]==0);cases++;}
fault=0;init_priv(&p,&d);p.regmap=&map;p.macro_params[0].clk_div_get=divisor;assert(!lpass_cdc_dmic_clk_enable_active(&c,0,0,true));assert(!lpass_cdc_dmic_clk_enable_active(&c,1,1,true));assert(p.dmic_0_1_tx_clk_cnt==1&&p.dmic_0_1_va_clk_cnt==1&&p.dmic_0_1_clk_div==1);assert(!lpass_cdc_dmic_clk_enable_active(&c,0,1,false));assert(p.dmic_0_1_clk_div==2);assert(!lpass_cdc_dmic_clk_enable_active(&c,0,0,false));assert(!p.dmic_0_1_clk_div&&!p.dmic_0_1_tx_clk_cnt);assert(lpass_cdc_dmic_clk_enable_active(&c,0,0,false)==-EINVAL);assert(lpass_cdc_dmic_clk_enable_active(&c,8,0,true)==-EINVAL);assert(lpass_cdc_dmic_clk_enable_active(&c,0,2,true)==-EINVAL);cases+=7;
printf("{\"dmic\":%d}\n",cases);}
