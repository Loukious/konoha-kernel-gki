#define WEIGHT_SCALE 10
#define MEAN_CUR_NUM 5
#define MEAN_VOL_NUM 5
#define DISCHARGING_MAX_EMPTY_COUNT 2
#define FAST_CHARGING_MAX_EMPTY_COUNT 10
#define NORMAL_CHARGING_MAX_EMPTY_COUNT 2
#define VABTT_EMPTY_HY 100
#define STRATEGY_FG_SOC_DECIMAL_DEFAULT_RATE 10
#define CHARGE_DFX_FG_IIC_ERR 1
#define CHARGE_DFX_BATTERY_CYCLECOUNT 2
static int fail_read,read_step,fw_error,original_error;
static struct fg_batt_info sample;
static int counters[4];
static int platform_fg_ops_get_dod_count(int n){(void)n;read_step=1;return fail_read==1?-EIO:0;}
static int get_count(int *v,int n){read_step=n;if(fail_read==n)return -EIO;*v=counters[n-2];return 0;}
static int platform_fg_ops_get_count_level1(int n,int *v){(void)n;return get_count(v,2);}
static int platform_fg_ops_get_count_level2(int n,int *v){(void)n;return get_count(v,3);}
static int platform_fg_ops_get_count_level3(int n,int *v){(void)n;return get_count(v,4);}
static int platform_fg_ops_get_count_lowtemp(int n,int *v){(void)n;return get_count(v,5);}
static int platform_fg_ops_get_batt_info(int n,struct fg_batt_info *v){(void)n;if(fail_read)return -EIO;*v=sample;return 0;}
static int strategy_fg_get_error_state(struct strategy_fg *f){f->fg_error=fw_error;return 0;}
static int platform_fg_ops_get_original_temp(int n,int *v){(void)n;if(original_error)return -EIO;*v=270;return 0;}
static int strategy_fg_check_extreme_cold_temp_compensation(struct strategy_fg *f,int t){(void)f;return t;}
static int strategy_fg_get_fast_charge(struct strategy_fg *f){(void)f;return 0;}
static int mca_charge_mievent_report(int a,void *v,int n){(void)a;(void)v;(void)n;return 0;}
static int strategy_fg_set_clear_count_data(struct strategy_fg *f){(void)f;return 0;}
static void ktime_get_boottime_ts64(struct timespec64 *v){v->tv_sec=200;}
static int get_smem_battery_info(int *v){*v=0;return 0;}
/* STAGED_DEFINITIONS */
int main(void){
 struct strategy_fg f={.fg_init_flag=true,.fake_soc=-1,.fake_temp=-999,.sample_error=-EAGAIN,.weight={28,20,14}};
 int value=123;
 for(int n=1;n<=5;n++){fail_read=n;assert(strategy_fg_get_dod_count(&f,&value)==0&&value==0);f.support_dod_vcutoff=true;value=123;assert(strategy_fg_get_dod_count(&f,&value)==-EIO&&value==123);f.support_dod_vcutoff=false;}
 f.support_dod_vcutoff=true;fail_read=0;counters[0]=INT_MAX;counters[1]=INT_MAX;counters[2]=INT_MAX;assert(strategy_fg_get_dod_count(&f,&value)==-ERANGE);counters[0]=10;counters[1]=20;counters[2]=30;assert(!strategy_fg_get_dod_count(&f,&value)&&value==110);f.weight[0]=INT_MAX;assert(strategy_fg_get_dod_count(&f,&value)==-ERANGE);f.weight[0]=28;f.support_dod_vcutoff=false;
 sample=(struct fg_batt_info){.fcc=1000000,.rm=500000,.rsoc=50,.soh=95,.cycle_count=10,.volt=3900,.temp=250,.curr=-500000};
 assert(!mca_strategy_update_fg_info(&f)&&f.batt_raw_soc==5000&&f.original_temp==270);
 sample.fcc=0;assert(mca_strategy_update_fg_info(&f)==-ERANGE&&f.batt_fcc==1000000);sample.fcc=-1;assert(mca_strategy_update_fg_info(&f)==-ERANGE);sample.fcc=1000000;sample.rsoc=101;assert(mca_strategy_update_fg_info(&f)==-ERANGE);sample.rsoc=50;original_error=1;assert(mca_strategy_update_fg_info(&f)==-EIO);original_error=0;
 int arithmetic=0;
 for(int capacity=1;capacity<=100000;capacity+=137){sample.fcc=capacity;for(int rm=0;rm<=capacity+100;rm+=59){sample.rm=rm;assert(!mca_strategy_update_fg_info(&f));assert(f.batt_raw_soc==min_t(s64,10000,(s64)rm*10000/capacity));arithmetic++;}}
 sample.fcc=INT_MAX;sample.rm=INT_MAX;assert(!mca_strategy_update_fg_info(&f)&&f.batt_raw_soc==10000);sample.rm=0;assert(!mca_strategy_update_fg_info(&f)&&!f.batt_raw_soc);
 fw_error=1;assert(mca_strategy_update_fg_info(&f)==-EIO);fw_error=0;
 value=777;assert(strategy_fg_ops_get_curr(&f,&value)==-EAGAIN&&value==777);f.sample_error=0;f.batt_current=INT_MAX;assert(!strategy_fg_ops_get_curr(&f,&value)&&value==INT_MAX);assert(strategy_fg_ops_get_curr(&f,NULL)==-EINVAL);f.sample_error=-EREMOTEIO;assert(strategy_fg_ops_get_temp(&f,&value)==-EREMOTEIO);f.fake_soc=38;assert(strategy_fg_ops_get_soc(&f)==38);f.fake_soc=-1;assert(strategy_fg_ops_get_soc(&f)==-EREMOTEIO);
 f.sample_error=0;for(int n=0;n<100;n++){f.batt_current=INT_MAX;strategy_fg_record_curr_mean(&f);assert(f.batt_current_mean==INT_MAX);}f.current_count=0;f.current_cursor=0;memset(f.current_history,0,sizeof(f.current_history));for(int n=0;n<100;n++){f.batt_current=INT_MIN;strategy_fg_record_curr_mean(&f);assert(f.batt_current_mean==INT_MIN);}
 f.vcutoff_sw=3000;f.vcutoff_shutdown_delay=3200;f.chg_status=POWER_SUPPLY_STATUS_CHARGING;f.fast_charge=true;f.batt_voltage=2900;for(int n=0;n<11;n++)strategy_fg_record_volt_mean(&f);assert(f.vbatt_empty&&f.empty_count==11);for(int n=0;n<10000;n++)strategy_fg_record_volt_mean(&f);assert(f.empty_count==11);f.batt_voltage=3800;for(int n=0;n<5;n++)strategy_fg_record_volt_mean(&f);assert(!f.vbatt_empty&&!f.empty_count);
 f.batt_ui_soc=50;f.batt_ui_soc_decimal=0;int rate;assert(!strategy_fg_ops_get_soc_decimal(&f,&value,&rate)&&value==0&&rate==10);
 printf("{\"raw_soc_cases\":%d,\"failed_reads_preserve_outputs\":true,\"signed_filter_extremes\":true,\"empty_hysteresis_and_saturation\":true}\n",arithmetic);
}
