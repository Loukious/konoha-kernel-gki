/* SERVICES */
#define MAX_RETRY_ATTEMPTS 500
#define SWR_DEVICE_SSR_UP 1
#define SWR_DEVICE_SSR_DOWN 2
static int sleeps,reset_id,notify_result;
static void msleep(int n){assert(n==20);sleeps++;}
static int lpass_cdc_va_macro_notify(struct lpass_cdc_va_macro_priv*p,u32 e,void*d){return notify_result;}
static int lpass_cdc_rsc_clk_reset(struct device*d,int id){reset_id=id;return fail()?-EIO:0;}
/* SOURCE */
int main(void){struct device parent={0},d={.parent=&parent};struct lpass_cdc_va_macro_priv p;struct snd_soc_component c={.dev=&parent};macro_dev=&d;init_va(&p,&d);p.component=&c;int cases=0;
p.va_mclk_users=1;assert(lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_WAIT_VA_CLK_RESET,0)==-ETIMEDOUT&&sleeps==500);p.va_mclk_users=0;assert(!lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_WAIT_VA_CLK_RESET,0));cases+=2;
for(int f=1;f<=3;f++){steps=0;fault=f;assert(lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_PRE_SSR_UP,0)==-EIO);assert(!p.core_vote_users&&!d.pm_refs);if(f==3){assert(p.reset_clock_owned&&clock_refs[3]==1);fault=0;assert(!lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_PRE_SSR_UP,0));assert(!p.reset_clock_owned&&!clock_refs[3]);}else assert(!clock_refs[3]);cases++;}
fault=0;assert(!lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_CLK_RESET,0)&&reset_id==3);fault=steps+1;assert(lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_CLK_RESET,0)==-EIO);fault=0;p.swr_ctrl_data=(void*)1;notify_result=-EIO;assert(lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_SSR_UP,0)==-EIO&&!p.dev_up);assert(lpass_cdc_va_macro_event_handler(&c,LPASS_CDC_MACRO_EVT_SSR_DOWN,0)==-EIO&&!p.pre_dev_up);cases+=4;printf("{\"ssr\":%d}\n",cases);}
