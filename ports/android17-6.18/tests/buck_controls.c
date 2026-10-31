int main(void) {
 struct platform_device dev={0};reset_fault();assert(!strategy_buckchg_class_probe(&dev));struct strategy_buckchg_dev *i=dev.data;
 char value[64];int cases=0;
 for(int n=-100;n<=30100;n+=17) {
  snprintf(value,sizeof(value),"%d",n);int expected=n>=0&&n<=30000?0:-EINVAL;
  assert(strategy_buckchg_if_set_chg_cur("test",value,i)==expected);
  assert(strategy_buckchg_if_set_input_cur("test",value,i)==expected);cases+=2;
 }
 char large[65];memset(large,'a',64);large[64]=0;
 assert(strategy_buckchg_if_set_input_cur(large,"1",i)==-EINVAL);
 assert(strategy_buckchg_if_set_chg_cur("","1",i)==-EINVAL);
 const char *bad[]={"","-1","2147483648","9999999999999999999999","12x"};
 for(unsigned n=0;n<ARRAY_SIZE(bad);n++)assert(strategy_buckchg_if_set_chg_cur("test",(char *)bad[n],i)==-EINVAL);
 assert(!strategy_buckchg_if_set_input_cur("shell","3000",i));assert(override_calls==1&&last_vote==1500);
 assert(!strategy_buckchg_if_set_input_cur("shell","0",i));assert(override_calls==2&&!i->input_limit_voter->enabled);
 assert(strategy_buckchg_if_set_chg_en("test",2,i)==-EINVAL);
 assert(strategy_buckchg_if_set_ship_mode("test",2,i)==-EINVAL);
 assert(strategy_buckchg_if_set_input_suspend("test","2",i)==-EINVAL);
 provider_error=-EAGAIN;
 assert(strategy_buckchg_if_set_chg_cur("test","3000",i)==-EAGAIN);
 assert(strategy_buckchg_if_set_input_suspend("test","1",i)==-EAGAIN);
 assert(strategy_buckchg_if_set_ship_mode("test",1,i)==-EAGAIN);
 provider_error=0;
 mutex_lock(&i->policy_lock);
 assert(!strategy_buckchg_soc_limit_sts_callback(i,1));assert(i->soc_limit_stepper_work.work.pending);
 assert(!mca_charger_buckchg_pwr_boost_sts_callback(i,1));
 assert(!strategy_buckchg_smartchg_set_fcc_callback(i,1000));
 mutex_unlock(&i->policy_lock);
 assert(strategy_buckchg_soc_limit_sts_callback(i,2)==-EINVAL);
 assert(mca_charger_buckchg_pwr_boost_sts_callback(i,-1)==-EINVAL);
 assert(strategy_buckchg_smartchg_set_fcc_callback(i,30001)==-EINVAL);
 int got=777;online_error=1;assert(strategy_buckchg_get_status(STRATEGY_STATUS_TYPE_ONLINE,&got,i)==-EIO&&got==777);online_error=0;
 assert(!strategy_buckchg_validate_config(i));int saved=i->sw_cv_fcc_step;i->sw_cv_fcc_step=0;assert(strategy_buckchg_validate_config(i)==-ERANGE);i->sw_cv_fcc_step=saved;
 i->in_dcp=UINT32_MAX;assert(strategy_buckchg_validate_config(i)==-ERANGE);i->in_dcp=0;
 i->support_multi_buck=1;assert(strategy_buckchg_validate_config(i)==-EOPNOTSUPP);i->support_multi_buck=0;
 i->sw_cv_fcc_step=-1;assert(strategy_buckchg_validate_config(i)==-ERANGE);i->sw_cv_fcc_step=saved;test_stock_config();
 int old_voltage=i->proc_data.vbat,old_current=i->proc_data.ibat;
 for(int fault_index=1;fault_index<=3;fault_index++){sensor_error=fault_index;assert(strategy_buckchg_read_battery(i)==-EIO);assert(i->proc_data.vbat==old_voltage&&i->proc_data.ibat==old_current);}
 sensor_error=0;sensor_current=INT32_MIN;assert(strategy_buckchg_read_battery(i)==-ERANGE);sensor_current=-1000000;
 sensor_voltage=INT32_MAX;assert(strategy_buckchg_read_battery(i)==-ERANGE);sensor_voltage=4000;
 sensor_temperature=INT32_MIN;assert(strategy_buckchg_read_battery(i)==-ERANGE);sensor_temperature=250;
 assert(!strategy_buckchg_read_battery(i));assert(i->proc_data.vbat==4000&&i->proc_data.ibat==-1000000);
 struct strategy_buckchg_dev second=*i;i->soc_step=0;second.soc_step=0;
 strategy_buckchg_process_soc_limit_change_more(1,i);strategy_buckchg_process_soc_limit_change_more(1,&second);strategy_buckchg_process_soc_limit_change_more(1,&second);
 assert(i->soc_step==1&&second.soc_step==2);
 for(int step=2;step<=10;step++){strategy_buckchg_process_soc_limit_change_more(1,i);assert(i->soc_step==step);}
 strategy_buckchg_process_soc_limit_change_more(1,i);assert(i->soc_step==10);
 strategy_buckchg_process_soc_limit_change_more(0,i);assert(!i->soc_step&&second.soc_step==2);
 strategy_buckchg_class_remove(&dev);assert(strategy_buckchg_if_set_chg_cur("test","1",i)==-ENODEV);free_info();
 printf("{\"current_bounds_cases\":%d,\"malformed_inputs\":7,\"shell_override_release\":true,\"provider_errors\":true,\"smart_callbacks_deferred_without_policy_lock\":true,\"device_tree_numeric_bounds\":true,\"battery_read_error_and_range_cases\":6,\"independent_soc_history_and_stage_bounds\":true}\n",cases);
}
