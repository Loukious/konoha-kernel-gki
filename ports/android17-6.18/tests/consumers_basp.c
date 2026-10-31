static void *wired_worker(void *arg) {
 (void)arg;mutex_lock(&qc.policy_lock);
 pthread_mutex_lock(&phase_lock);phase=1;pthread_cond_broadcast(&phase_cond);while(phase<2)pthread_cond_wait(&phase_cond,&phase_lock);pthread_mutex_unlock(&phase_lock);
 mutex_lock(&jeita.state_lock);mutex_unlock(&jeita.state_lock);mutex_unlock(&qc.policy_lock);return NULL;
}
static void *remove_worker(void *arg) {mca_quick_charge_remove(arg);return NULL;}
static void join_bounded(pthread_t thread) {struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);ts.tv_sec+=2;assert(!pthread_timedjoin_np(thread,NULL,&ts));}
int main(void) {
 int faults=0,invalid=0,transactions=0;
 setup();assert(!smart_basp_dispatch(&transport,&decoded));assert(qc.batt_para[0].temp_info[0].volt_info.volt_para[1].voltage==4500);assert(jeita.jeita_para.jeita_data[0].vterm==4500);cleanup();transactions++;
 for(int i=1;i<=6;i++) {
  setup();void *old=qc.batt_para[0].temp_info[0].volt_info.volt_para;fail_at=i;
  assert(smart_basp_dispatch(&transport,&decoded)==-ENOMEM);assert(old==qc.batt_para[0].temp_info[0].volt_info.volt_para);assert(jeita.jeita_para.jeita_data[0].vterm==4600);assert(!qc.pending_basp && !jeita.pending_basp);fail_at=0;cleanup();faults++;
 }
 for(int idx=-20;idx<20;idx++) {
  setup();specs[0].t_range.idx=idx;
  int ret=smart_basp_dispatch(&transport,&decoded);
  if(idx==0){assert(!ret);transactions++;}else{assert(ret==-EINVAL);assert(jeita.jeita_para.jeita_data[0].vterm==4600);invalid++;}cleanup();
 }
 for(int size=0;size<=MCA_BASP_MAX_STEPS+1;size++) {
  setup();specs[0].step_size=size;if(size>2)specs[0].steps=NULL;
  if(size>=1 && size<=2){assert(!smart_basp_dispatch(&transport,&decoded));transactions++;}else{assert(smart_basp_dispatch(&transport,&decoded)==-EINVAL);invalid++;}cleanup();
 }
 setup();mock_ops[MCA_SMARTCHG_IF_CHG_TYPE_QC]=NULL;assert(smart_basp_dispatch(&transport,&decoded)==-ENODEV);assert(jeita.jeita_para.jeita_data[0].vterm==4600);cleanup();
 for(int n=0;n<200;n++) {
  setup();transaction_thread=pthread_self();tracked_jeita=&jeita.state_lock;track_cycle=true;phase=0;
  pthread_t worker;assert(!pthread_create(&worker,NULL,wired_worker,NULL));
  pthread_mutex_lock(&phase_lock);while(phase<1)pthread_cond_wait(&phase_cond,&phase_lock);pthread_mutex_unlock(&phase_lock);
  assert(smart_basp_dispatch(&transport,&decoded)==-EBUSY);assert(jeita.jeita_para.jeita_data[0].vterm==4600);join_bounded(worker);track_cycle=false;cleanup();
 }
 setup();struct mca_quick_charge_volt_para_info *v=&qc.batt_para[0].temp_info[0].volt_info;
 for(int i=0;i<4;i++)v->stage_para[i]=1;
 assert(mca_quick_charge_jump_stage(-20,v)>=0);assert(mca_quick_charge_jump_stage(100,v)==3);
 assert(!mca_quick_charge_chg_disable_voter_cb(NULL,&qc,1,"soc_limit"));assert(last_scheduled==&qc.stop_work && scheduled>0 && stopped==0);
 /* Removal waits on the registered provider without holding policy_lock. */
 assert(!pthread_rwlock_rdlock(&smart_ops_srcu));assert(!mca_qc_basp_prepare(&qc,&decoded.update[1]));
 struct platform_device p={.data=&qc};pthread_t remover;removal_entered=0;assert(!pthread_create(&remover,NULL,remove_worker,&p));
 while(!READ_ONCE(removal_entered))sched_yield();mca_qc_basp_commit(&qc);assert(!pthread_rwlock_unlock(&smart_ops_srcu));join_bounded(remover);
 assert(disabled==3 && stopped==1 && voters_destroyed==1 && !qc.batt_para[0].temp_info);cleanup();
 printf("{\"allocation_faults\":%d,\"invalid_updates\":%d,\"successful_transactions\":%d,\"lock_cycle_iterations\":200,\"provider_removal_drain\":true,\"deferred_election_stop\":true}\n",faults,invalid,transactions);
}
