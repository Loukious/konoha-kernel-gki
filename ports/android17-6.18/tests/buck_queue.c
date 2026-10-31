static void *buck_thread(void *arg) {
 struct strategy_buckchg_dev *i=arg;strategy_buckchg_monitor_workfunc(&i->monitor_work.work);return NULL;
}
static void *wire_thread(void *arg) {
 struct strategy_buckchg_dev *i=arg;pthread_barrier_wait(&buck_entered);
 assert(!pthread_mutex_lock(&wired_lock));pthread_barrier_wait(&wire_entered);
 assert(!strategy_buckchg_process_event(10,20,i));int online=8;
 assert(!strategy_buckchg_get_status(STRATEGY_STATUS_TYPE_ONLINE,&online,i));
 assert(!pthread_mutex_unlock(&wired_lock));return NULL;
}
struct producer { struct strategy_buckchg_dev *info; int id; int accepted; };
static atomic_bool producers_done;
static void *produce(void *arg) {
 struct producer *p=arg;
 for(int j=0;j<1000;j++){int ret=strategy_buckchg_process_event(p->id,j,p->info);assert(!ret||ret==-ENOSPC);if(!ret)p->accepted++;}
 return NULL;
}
static void *consume(void *arg) {
 struct strategy_buckchg_dev *i=arg;
 do {strategy_buckchg_event_workfunc(&i->event_work);sched_yield();}while(!atomic_load(&producers_done));
 strategy_buckchg_event_workfunc(&i->event_work);return NULL;
}
int main(void) {
 alarm(25);struct platform_device dev={0};reset_fault();assert(!strategy_buckchg_class_probe(&dev));struct strategy_buckchg_dev *i=dev.data;
 assert(strategy_buckchg_process_event(MCA_EVENT_WIRELESS_REVCHG,1,i)==-EOPNOTSUPP);
 int ordered=0;
 for(int round=0;round<3;round++) {
  for(int n=0;n<64;n++)assert(!strategy_buckchg_process_event(round,n,i));
  assert(strategy_buckchg_process_event(99,99,i)==-ENOSPC);
  strategy_buckchg_event_workfunc(&i->event_work);
  for(int n=0;n<64;n++){assert(seen[ordered].event==round&&seen[ordered].value==n);ordered++;}
 }
 lock_cycle=true;i->monitor_active=true;int cycles=200;
 pthread_barrier_init(&buck_entered,NULL,2);pthread_barrier_init(&wire_entered,NULL,2);
 for(int n=0;n<cycles;n++) {
  pthread_t buck,wire;assert(!pthread_create(&buck,NULL,buck_thread,i));assert(!pthread_create(&wire,NULL,wire_thread,i));
  assert(!pthread_join(buck,NULL));assert(!pthread_join(wire,NULL));strategy_buckchg_event_workfunc(&i->event_work);
 }
 pthread_barrier_destroy(&buck_entered);pthread_barrier_destroy(&wire_entered);lock_cycle=false;
 pthread_t writer[4],reader;struct producer producers[4];int before=handled,total=0;
 assert(!pthread_create(&reader,NULL,consume,i));
 for(int n=0;n<4;n++){producers[n]=(struct producer){i,n,0};assert(!pthread_create(&writer[n],NULL,produce,&producers[n]));}
 for(int n=0;n<4;n++){assert(!pthread_join(writer[n],NULL));total+=producers[n].accepted;}
 atomic_store(&producers_done,true);assert(!pthread_join(reader,NULL));assert(handled-before==total);
 strategy_buckchg_class_remove(&dev);free_info();
 printf("{\"queue_fifo_cases\":%d,\"overflow_cases\":3,\"buck_wired_lock_cycles\":%d,\"concurrent_producer_attempts\":4000,\"accepted_events_drained\":%d}\n",ordered,cycles,total);
}
