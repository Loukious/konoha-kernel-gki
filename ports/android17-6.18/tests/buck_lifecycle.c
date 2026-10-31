static void *run_monitor(void *arg) { struct strategy_buckchg_dev *i=arg;strategy_buckchg_monitor_workfunc(&i->monitor_work.work);return NULL; }
static void *remove_device(void *arg) { strategy_buckchg_class_remove(arg);return NULL; }
int main(void) {
 struct platform_device dev={0};reset_fault();
 assert(!strategy_buckchg_class_probe(&dev));int checkpoints=sequence;
 assert(live_voters==11 && live_wq==1 && registrations==5);
 strategy_buckchg_class_shutdown(&dev);assert(!live_voters&&!live_wq&&!registrations);
 int drains=disables;strategy_buckchg_class_remove(&dev);assert(disables==drains);free_info();
 for(int f=1;f<=checkpoints;f++) {
  reset_fault();fail_at=f;int ret=strategy_buckchg_class_probe(&dev);
  assert(ret<0);assert(!live_voters&&!live_wq&&!registrations&&!dev.data);free_info();
 }
 reset_fault();assert(!strategy_buckchg_class_probe(&dev));struct strategy_buckchg_dev *i=dev.data;
 assert(!strategy_buckchg_process_event(7,8,i));strategy_buckchg_class_remove(&dev);
 assert(strategy_buckchg_process_event(7,8,i)==-ENODEV);assert(!handled);
 assert(!live_voters&&!live_wq&&!registrations);free_info();
 reset_fault();assert(!strategy_buckchg_class_probe(&dev));i=dev.data;i->monitor_active=true;drain_work=true;
 pthread_barrier_init(&work_entered,NULL,2);pthread_barrier_init(&work_release,NULL,2);pthread_barrier_init(&drain_started,NULL,2);
 assert(!pthread_create(&running_work,NULL,run_monitor,i));pthread_barrier_wait(&work_entered);
 pthread_t removal;assert(!pthread_create(&removal,NULL,remove_device,&dev));pthread_barrier_wait(&drain_started);
 assert(i->stopping&&live_voters==11&&live_wq==1&&!registrations);
 assert(strategy_buckchg_process_event(1,1,i)==-ENODEV);
 pthread_barrier_wait(&work_release);assert(!pthread_join(removal,NULL));
 assert(!live_voters&&!live_wq&&!registrations);free_info();
 pthread_barrier_destroy(&work_entered);pthread_barrier_destroy(&work_release);pthread_barrier_destroy(&drain_started);
 printf("{\"probe_failure_points\":%d,\"owned_voters\":11,\"worker_drains_per_stop\":9,\"shutdown_remove_idempotent\":true,\"withdraw_before_drain\":true,\"active_work_removal_drains\":true}\n",checkpoints);
}
