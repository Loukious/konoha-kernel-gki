#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
typedef uint32_t u32;
struct hw_fence_driver_data {
 bool has_soccp;
 pthread_cond_t soccp_wait_queue;
 atomic_uint signaled_clients_mask;
};
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t progress = PTHREAD_COND_INITIALIZER;
static atomic_bool stopping;
static unsigned processed, seen_mask;
static bool waiting, processing, hold_processing;
#define atomic_read(p) atomic_load(p)
#define atomic_xchg(p,v) atomic_exchange(p,v)
static bool kthread_should_stop(void) { return atomic_load(&stopping); }
#define wait_event(queue,condition) do { \
 pthread_mutex_lock(&state_lock); \
 while (!(condition)) { \
  waiting=true;pthread_cond_broadcast(&progress); \
  assert(!pthread_cond_wait(&(queue),&state_lock)); \
  waiting=false; \
 } \
 pthread_mutex_unlock(&state_lock); \
} while (0)
static void hw_fence_utils_process_signaled_clients_mask(struct hw_fence_driver_data *d,u32 mask) {
 pthread_mutex_lock(&state_lock);
 processed++;seen_mask|=mask;processing=true;pthread_cond_broadcast(&progress);
 while(hold_processing)assert(!pthread_cond_wait(&progress,&state_lock));
 processing=false;pthread_mutex_unlock(&state_lock);
}
/* STAGED_DEFINITIONS */
static void *run_listener(void *data) { assert(!hw_fence_soccp_listener(data));return NULL; }
static void await_progress(void) {
 struct timespec until;assert(!clock_gettime(CLOCK_REALTIME,&until));until.tv_sec+=2;
 assert(!pthread_cond_timedwait(&progress,&state_lock,&until));
}
int main(void) {
 unsigned cases=0;
 for(unsigned round=0;round<200;round++)for(unsigned mode=0;mode<4;mode++) {
  struct hw_fence_driver_data d={.has_soccp=true,.soccp_wait_queue=PTHREAD_COND_INITIALIZER};
  atomic_init(&d.signaled_clients_mask,mode>=2?0x81:0);
  atomic_store(&stopping,mode==0);processed=seen_mask=0;
  waiting=processing=false;hold_processing=mode==3;
  pthread_t thread;assert(!pthread_create(&thread,NULL,run_listener,&d));
  pthread_mutex_lock(&state_lock);
  if(mode==1)while(!waiting)await_progress();
  if(mode==2)while(!processed||!waiting)await_progress();
  if(mode==3)while(!processing)await_progress();
  atomic_store(&stopping,true);
  if(mode==1)atomic_store(&d.signaled_clients_mask,0x40);
  hold_processing=false;
  pthread_cond_broadcast(&d.soccp_wait_queue);pthread_cond_broadcast(&progress);
  pthread_mutex_unlock(&state_lock);
  struct timespec until;assert(!clock_gettime(CLOCK_REALTIME,&until));until.tv_sec+=2;
  assert(!pthread_timedjoin_np(thread,NULL,&until));
  assert(processed==(mode>=2));assert(seen_mask==(mode>=2?0x81:0));
  assert(!pthread_cond_destroy(&d.soccp_wait_queue));cases++;
 }
 printf("{\"hwfence_listener_shutdown_cases\":%u}\n",cases);
}
