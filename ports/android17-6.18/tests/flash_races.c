static pthread_mutex_t gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond=PTHREAD_COND_INITIALIZER;
static bool entered,release_io;
static atomic_bool stop_started,stop_done;
static int operation_rc;
static void pause_io(void) {
 pthread_mutex_lock(&gate_lock);
 if(!entered){entered=true;pthread_cond_broadcast(&gate_cond);while(!release_io)pthread_cond_wait(&gate_cond,&gate_lock);}
 pthread_mutex_unlock(&gate_lock);
}
static void *query_thread(void *arg) {int output=-1;operation_rc=qti_flash_led_prepare(&trigger,QUERY_MAX_AVAIL_CURRENT,&output);assert(!operation_rc&&output==2000);return NULL;}
static void *on_thread(void *arg) {qti_flash_on_work(&snodes[0].on_work);return NULL;}
static void *stop_thread(void *arg) {atomic_store(&stop_started,true);qti_flash_led_stop(&led);atomic_store(&stop_done,true);return NULL;}
int main(void) {
 for(int mode=0;mode<2;mode++)for(int i=0;i<200;i++) {
  init();if(mode){configure(0,100);snodes[0].on_pending=true;snodes[0].off_time_ms=20;}
  entered=release_io=false;atomic_store(&stop_started,false);atomic_store(&stop_done,false);io_hook=pause_io;
  pthread_t worker,stopper;assert(!pthread_create(&worker,NULL,mode?on_thread:query_thread,NULL));pthread_mutex_lock(&gate_lock);while(!entered)pthread_cond_wait(&gate_cond,&gate_lock);pthread_mutex_unlock(&gate_lock);
  assert(!pthread_create(&stopper,NULL,stop_thread,NULL));while(!atomic_load(&stop_started))sched_yield();assert(!atomic_load(&stop_done));
  pthread_mutex_lock(&gate_lock);release_io=true;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate_lock);
  assert(!pthread_join(worker,NULL));assert(!pthread_join(stopper,NULL));io_hook=NULL;assert(atomic_load(&stop_done)&&led.stopping&&!led.ready);assert(!snodes[0].on_timer.active&&!snodes[0].off_timer.active&&!snodes[0].on_work.pending&&!snodes[0].off_work.pending);
  int before=io_calls;assert(qti_flash_timeout_set(&fnodes[0].fdev,10000)==-ENODEV);qti_flash_on_work(&snodes[0].on_work);qti_flash_off_work(&snodes[0].off_work);assert(io_calls==before);assert(qti_flash_led_set_param(&trigger,(struct flash_led_param){1,2})==-ENODEV);assert(qti_flash_led_prepare(&trigger,0,NULL)==-ENODEV);fini();
 }
 printf("{\"query_remove_races\":200,\"worker_remove_races\":200,\"post_remove_callbacks_rejected\":true}\n");
}
