#define EU_SOC_PROPORTION_C 100
#define EU_SOC_PROPORTION 101
#define XM_CHARGER_TYPE_SDP 1
#define MCA_BATTERY_SHUTDOWN_VCUTTOFF_FW 3050
#define MCA_BATTERY_SHUTDOWN_VCUTTOFF_SHUTDOWN_DELAY 3050
#define MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW 2900
#define MCA_BATTERY_SHUTDOWN_VCUTTOFF_MIN 2500
#define MCA_BATTERY_SHUTDOWN_VOLTAGE_HYST 50
#define MCA_HEAVY_LOAD_DISCHARGING_CURRENT 2000000
#define MCA_HEAVY_LOAD_DISCHARGING_COUNT 2
#define ROLLBACK_SHUTDOWN_VOLT_MAX 3000
enum shutdown_strategy_type{SHUTDOWN_STRATEGY_BATTERY_HEALTH,SHUTDOWN_STRATEGY_BATTERY_SPEC,SHUTDOWN_STRATEGY_MAX};
static time64_t now;
static time64_t ktime_get_boottime_seconds(void){return now;}
static int mca_smartchg_get_limit_soc(void){return 0;}
int mca_battery_shutdown_check_shutdown_delay(void *data,int *soc){(void)data;(void)soc;return 0;}
/* STAGED_DEFINITIONS */
int main(void){
 struct strategy_fg f={.cfg={.soc_proportion=97,.soc_proportion_c=96,.report_full_raw_soc=9700},.fake_soc=-1,.last_ui_soc=-1};
 int mapped=0;
 for(int raw=0;raw<=10000;raw++){f.batt_raw_soc=raw;fg_update_mapped_soc(&f);assert(f.batt_mapped_soc==(raw>=9700?100:min((raw+96)/97,99)));assert(f.batt_mapped_soc>=0&&f.batt_mapped_soc<=100);mapped++;}
 f.is_eu_model=true;f.chg_status=POWER_SUPPLY_STATUS_CHARGING;for(int raw=0;raw<=10000;raw++){f.batt_raw_soc=raw;fg_update_mapped_soc(&f);assert(f.batt_mapped_soc==min((raw+100)/101,99));mapped++;}f.is_eu_model=false;
 f.batt_raw_soc=4850;f.batt_rsoc=50;now=100;assert(fg_ui_soc_smooth(&f,false)==50);struct strategy_fg fresh=f;fresh.last_ui_soc=-1;fresh.smooth_last_time=0;fresh.batt_raw_soc=1940;fresh.batt_rsoc=20;assert(fg_ui_soc_smooth(&fresh,false)==20&&fresh.last_ui_soc!=f.last_ui_soc);
 f.batt_raw_soc=3880;f.batt_current_mean=100000;f.power_present=false;now=109;assert(fg_ui_soc_smooth(&f,true)==50);now=110;assert(fg_ui_soc_smooth(&f,true)==49);now=111;assert(fg_ui_soc_smooth(&f,true)==49);now=120;assert(fg_ui_soc_smooth(&f,true)==48);f.vbatt_empty=true;now=130;assert(fg_ui_soc_smooth(&f,true)==0);
 f=(struct strategy_fg){.vcutoff_shutdown_delay=3200,.smooth={.smooth_temp_unit={0,150},.smooth_vol_offset={INT_MIN,0,100,200,300},.smooth_curr_unit={0,300000,600000,1000000}},.batt_voltage_mean=3000,.batt_current_mean=100000,.batt_temperature=-200};for(int i=0;i<5;i++)for(int j=0;j<4;j++){f.smooth.dischg_smooth_low_temp_unit[i][j]=10;f.smooth.dischg_smooth_high_temp_unit[i][j]=500;}int keep=30;get_unit_time_low_soc(&f,&keep);assert(keep==10);f.batt_temperature=200;get_unit_time_low_soc(&f,&keep);assert(keep==500);
 struct mca_battery_shutdown_vcutoff_para low[]={{600,2900,2850,2800},{1200,3150,3100,3050},{65535,3250,3200,3150}};
 struct mca_battery_shutdown_vcutoff_para high[]={{200,2900,2850,2800},{600,3050,3050,3000},{1200,3150,3150,3100},{65535,3250,3250,3200}};
 struct mca_battery_shutdown_temp_para_info temps[]={{{-65535,0,0,2},{3,low}},{{0,65535,2,0},{4,high}}};
 f=(struct strategy_fg){.support_cc_vcutoff=true,.batt_health_para={.temp_para_size=2,.temp_info=temps},.batt_temperature=-10,.batt_cyclecount=400};assert(!mca_battery_shutdown_validate_tables(&f));assert(!mca_battery_shutdown_update_vcutoff_para(&f)&&f.vcutoff_fw==2900);f.batt_temperature=10;assert(!mca_battery_shutdown_update_vcutoff_para(&f)&&f.batt_health_para.cur_temp_index==0);f.batt_temperature=20;assert(!mca_battery_shutdown_update_vcutoff_para(&f)&&f.batt_health_para.cur_temp_index==1&&f.vcutoff_fw==3050);f.batt_temperature=-10;assert(!mca_battery_shutdown_update_vcutoff_para(&f)&&f.batt_health_para.cur_temp_index==1);f.batt_temperature=-30;assert(!mca_battery_shutdown_update_vcutoff_para(&f)&&f.batt_health_para.cur_temp_index==0);
 low[0].vcutoff_fw=0;assert(mca_battery_shutdown_validate_tables(&f)==-EINVAL);low[0].vcutoff_fw=2900;low[1].range_upper=100;assert(mca_battery_shutdown_validate_tables(&f)==-EINVAL);low[1].range_upper=1200;temps[1].temp_para.temp_low=-1;assert(mca_battery_shutdown_validate_tables(&f)==-EINVAL);temps[1].temp_para.temp_low=0;f.batt_health_para.temp_info=NULL;assert(mca_battery_shutdown_validate_tables(&f)==-EINVAL);
 f=(struct strategy_fg){.vcutoff_shutdown_delay=3200,.batt_voltage_mean=3190,.batt_current=2000001};for(int n=0;n<100000;n++)mca_battery_shutdown_heavy_load_check(&f);assert(f.heavy_load_count==3);f.batt_voltage_mean=3800;assert(!mca_battery_shutdown_heavy_load_check(&f)&&!f.heavy_load_count);
 printf("{\"mapping_cases\":%d,\"per_device_smoothing\":true,\"time_and_empty_constraints\":true,\"cutoff_hysteresis\":true,\"malformed_tables_rejected\":true,\"heavy_load_count_saturates\":true}\n",mapped);
}
