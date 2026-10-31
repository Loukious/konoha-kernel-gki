/* SERVICES */
#define LPASS_CDC_WCD_EVT_SSR_DOWN 3
#define LPASS_CDC_WCD_EVT_SSR_UP 4
static int down,up,all;
static void regcache_cache_only(struct regmap*m,bool v){m->cache_only=v;}
static void regcache_mark_dirty(struct regmap*m){}
static int regcache_sync(struct regmap*m){return fail()?-EIO:0;}
static void usleep_range(int a,int b){}
static int lpass_cdc_clk_rsc_all_clocks(struct device*d,bool en){if(en){if(fail())return -EIO;all++;}else assert(all-->0);return 0;}
static void lpass_cdc_notifier_call(struct lpass_cdc_priv*p,int event){if(event==LPASS_CDC_WCD_EVT_SSR_DOWN)down++;else up++;}
static int rsc_cb(struct device*d,u16 event){if(event==LPASS_CDC_MACRO_EVT_SSR_DOWN)return 0;return fail()?-EIO:0;}
static int macro_cb(struct snd_soc_component*c,u16 event,u32 data){if(event==LPASS_CDC_MACRO_EVT_SSR_DOWN)return 0;return fail()?-EIO:0;}
/* SOURCE */
int main(void){struct device d={0},r={.parent=&d};struct lpass_cdc_priv p;static struct regmap map;int cases=0;
for(int f=1;f<=7;f++){init_priv(&p,&d);p.regmap=&map;p.clk_dev=&r;p.rsc_clk_cb=rsc_cb;p.macro_params[0].event_handler=macro_cb;steps=0;fault=f;down=up=all=0;assert(lpass_cdc_ssr_enable_active(&d,&p)==-EIO);assert(down==1&&!up&&!all&&!p.dev_up&&!p.pre_dev_up&&map.cache_only);cases++;}
fault=0;assert(!lpass_cdc_ssr_enable_active(&d,&p));assert(up==1&&p.dev_up&&p.pre_dev_up&&!all&&!map.cache_only);lpass_cdc_ssr_disable_active(&d,&p);assert(!p.dev_up&&!p.pre_dev_up&&map.cache_only);cases+=2;printf("{\"ssr\":%d}\n",cases);}
