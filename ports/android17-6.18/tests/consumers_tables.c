int main(void) {
 int controls=0,voltages=0,invalid=0;
 setup();assert(!mca_qc_validate_batt_para(&qc.batt_para[0]));assert(!mca_jeita_validate_table(jeita.jeita_para.jeita_data,1));
 struct mca_quick_charge_temp_para_info *entry=&qc.batt_para[0].temp_info[0];
 struct mca_buckchg_jeita_data *jt=jeita.jeita_para.jeita_data;
 int bad[]={-1,0,5001,INT32_MAX};
 for(size_t i=0;i<ARRAY_SIZE(bad);i++) {
  int saved=entry->temp_para.normal_max_fv;entry->temp_para.normal_max_fv=bad[i];assert(mca_qc_validate_batt_para(&qc.batt_para[0])==-EINVAL);entry->temp_para.normal_max_fv=saved;
  saved=jt->vterm;jt->vterm=bad[i];assert(mca_jeita_validate_table(jt,1)==-EINVAL);jt->vterm=saved;invalid+=2;
 }
 entry->volt_info.volt_para[1].voltage=4200;assert(mca_qc_validate_batt_para(&qc.batt_para[0])==-EINVAL);entry->volt_info.volt_para[1].voltage=4600;invalid++;
 jt->volt_para.size=1;jt->volt_para.volt_data=NULL;assert(mca_jeita_validate_table(jt,1)==-EINVAL);jt->volt_para.size=0;invalid++;
 for(int mode=-10;mode<=10;mode++)for(int value=-10;value<=10;value++) {
  char buf[100];snprintf(buf,sizeof(buf),"div%d_master %d",mode,value);
  int ret=mca_quick_charge_set_cp_path_enable(buf,&qc);
  bool valid=(mode==1 || mode==2 || mode==4) && (value==0 || value==1);
  assert((ret==0)==valid);controls++;
 }
 int old[CHG_MODE_MAX][CP_ROLE_MAX];memcpy(old,qc.sysfs_data.cp_path_enable,sizeof(old));
 assert(mca_quick_charge_set_cp_path_enable("div1_master 1 div5_slave 0",&qc)==-EINVAL);assert(!memcmp(old,qc.sysfs_data.cp_path_enable,sizeof(old)));
 char large[5000];memset(large,'a',sizeof(large));memcpy(large,"div1_",5);large[sizeof(large)-1]=0;assert(mca_quick_charge_set_cp_path_enable(large,&qc)==-EINVAL);
 assert(!mca_quick_charge_set_cp_path_enable("div1_master 0\n div2_slave 1 \t",&qc));
 struct mca_buckchg_jeita_volt_data vd[]={{4200,8000},{4600,4000}};jt->volt_para.size=2;jt->volt_para.volt_data=vd;
 for(measured_voltage=1;measured_voltage<10000;measured_voltage++) {
  int idx=3,cur=mca_buckchg_jeita_get_curr(jt,&idx);
  assert(cur==(measured_voltage<=4200?8000:4000));
  assert(idx==(measured_voltage<=4200?0:measured_voltage<=4600?1:-1));voltages++;
 }
 voltage_error=-EIO;int idx=2;assert(mca_buckchg_jeita_get_curr(jt,&idx)==0 && idx==-1);voltage_error=0;
 jt->volt_para.size=0;jt->volt_para.volt_data=NULL;cleanup();
 printf("{\"control_cases\":%d,\"voltage_cases\":%d,\"invalid_table_cases\":%d,\"atomic_control_parse\":true,\"oversized_control_name\":true,\"voltage_read_error\":true}\n",controls,voltages,invalid);
}
