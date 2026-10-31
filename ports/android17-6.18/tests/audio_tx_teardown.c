/* SERVICES */
static bool pm_disabled,suspended;static int suspend_result;
static void pm_runtime_disable(struct device*d){assert(!d->pm_refs);pm_disabled=true;}
static bool pm_runtime_status_suspended(struct device*d){assert(pm_disabled);return suspended;}
static int lpass_cdc_runtime_suspend(struct device*d){assert(pm_disabled&&!suspended);return suspend_result;}
static void pm_runtime_set_suspended(struct device*d){assert(pm_disabled&&!suspend_result);suspended=true;}
/* SOURCE */
int main(void){struct device parent={0},d={.parent=&parent};struct lpass_cdc_tx_macro_priv p;struct snd_soc_component c={.dev=&parent};static struct regmap map;parent.regmap=&map;init_tx(&p,&d);p.component=&c;int cases=0;
assert(!lpass_cdc_tx_macro_mclk_enable(&p,true));assert(!lpass_cdc_tx_macro_mclk_enable(&p,true));p.dapm_mclk_enable=true;p.dmic_owned[7]=true;p.dmic_id[7]=3;dmic_refs[3]=1;p.stream_owned[1]=p.stream_pending[2]=3;p.dec_ref_cnt[0]=1;p.hs_muted_mask=1;assert(!lpass_cdc_tx_macro_component_cleanup(&p));assert(!p.dmic_owned[7]&&!dmic_refs[3]&&!p.dapm_mclk_enable&&p.tx_mclk_users==1&&clock_refs[0]==1&&!p.hs_muted_mask&&!p.stream_owned[1]&&!p.stream_pending[2]&&!p.dec_ref_cnt[0]);assert(!lpass_cdc_tx_macro_release_clocks(&p)&&!clock_refs[0]&&!fs_refs);cases+=3;
p.dmic_owned[1]=true;p.dmic_id[1]=3;dmic_refs[3]=1;fault=steps+1;assert(lpass_cdc_tx_macro_component_cleanup(&p)==-EIO&&p.dmic_owned[1]&&dmic_refs[3]==1);fault=0;assert(!lpass_cdc_tx_macro_release_clocks(&p)&&!p.dmic_owned[1]&&!dmic_refs[3]);cases+=2;
p.failed_clock_owned=true;clock_refs[0]=1;fault=steps+1;assert(lpass_cdc_tx_macro_release_clocks(&p)==-EIO&&p.failed_clock_owned&&clock_refs[0]==1);fault=0;assert(!lpass_cdc_tx_macro_release_clocks(&p)&&!p.failed_clock_owned&&!clock_refs[0]);cases+=2;
suspend_result=-EIO;lpass_cdc_tx_macro_disable_pm(&p);assert(pm_disabled&&!suspended);pm_disabled=false;suspend_result=0;lpass_cdc_tx_macro_disable_pm(&p);assert(pm_disabled&&suspended);cases+=2;printf("{\"teardown\":%d}\n",cases);}
