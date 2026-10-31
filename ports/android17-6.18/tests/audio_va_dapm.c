/* SERVICES */
static int lpass_cdc_va_macro_core_vote(void*,bool);
static int tx_mclks;
static int lpass_cdc_tx_mclk_enable(struct snd_soc_component*c,bool en){if(fail())return -EIO;if(en)tx_mclks++;else assert(tx_mclks-->0);return 0;}
/* SOURCE */
int main(void){struct device parent={0},d={.parent=&parent};macro_dev=&d;static struct regmap map;parent.regmap=&map;struct lpass_cdc_va_macro_priv p;struct snd_soc_component c={.dev=&parent};struct snd_soc_dapm_context dapm={.component=&c};struct snd_soc_dapm_widget w={.dapm=&dapm};int cases=0;
for(int f=1;f<=2;f++){init_va(&p,&d);p.component=&c;p.use_lpi_mixer_control=true;fault=f;steps=0;assert(lpass_cdc_va_macro_mclk_event(&w,NULL,1)==-EIO);assert(!p.dapm_mclk_active&&!p.dapm_tx_clk_status&&!tx_mclks&&!clock_refs[0]);cases++;}
fault=0;init_va(&p,&d);p.component=&c;p.use_lpi_mixer_control=true;assert(!lpass_cdc_va_macro_mclk_event(&w,NULL,1));assert(p.dapm_uses_tx&&tx_mclks==1);p.lpi_enable=true;assert(!lpass_cdc_va_macro_mclk_event(&w,NULL,8));assert(!tx_mclks&&!p.dapm_mclk_active&&!p.dapm_tx_clk_status&&!clock_refs[0]);cases+=2;
p.use_lpi_mixer_control=false;assert(!lpass_cdc_va_macro_mclk_event(&w,NULL,1));assert(p.va_mclk_users==1&&p.dapm_mclk_active);assert(!lpass_cdc_va_macro_enable_tx(&w,NULL,2));assert(!p.dapm_tx_clk_status);assert(!lpass_cdc_va_macro_enable_tx(&w,NULL,4));fault=steps+1;assert(lpass_cdc_va_macro_enable_tx(&w,NULL,2)==-EIO&&p.dapm_tx_clk_status==1);fault=0;assert(!lpass_cdc_va_macro_mclk_event(&w,NULL,8));assert(!p.va_mclk_users&&!p.dapm_mclk_active&&!p.dapm_tx_clk_status&&!clock_refs[0]&&!clock_refs[3]&&!fs_refs);cases+=5;printf("{\"dapm\":%d}\n",cases);}
