#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#define GFP_KERNEL 0
#define BTPOWER_CONFIG_MAX_TIMEOUT 600
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
struct list_head {struct list_head*next,*prev;};
#define INIT_LIST_HEAD(p) ((p)->next=(p)->prev=(p))
static void list_add_tail(struct list_head*n,struct list_head*h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head*n){n->prev->next=n->next;n->next->prev=n->prev;}
static int list_empty(struct list_head*h){return h->next==h;}
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
struct completion {int done;};typedef unsigned refcount_t;
static void init_completion(struct completion*c){c->done=0;}
static void complete(struct completion*c){c->done=1;}
static void refcount_set(refcount_t*r,int n){*r=n;}
static int refcount_dec_and_test(refcount_t*r){assert(*r);return --*r==0;}
#define mutex_lock(p) assert(!pthread_mutex_lock(p))
#define mutex_unlock(p) assert(!pthread_mutex_unlock(p))
static pthread_mutex_t btpower_operation_lock=PTHREAD_MUTEX_INITIALIZER;
struct work_struct {int pending;};
struct platform_pwr_data {pthread_mutex_t pwr_mtx;struct list_head request_queue;unsigned queued_requests;struct work_struct wq_pwr_voting;};
static struct platform_pwr_data pdata={.pwr_mtx=PTHREAD_MUTEX_INITIALIZER},*pwr_data=&pdata;
static int live,alloc_fail,wait_mode,delivered,requests[128];static void*system_highpri_wq;
static void*kzalloc(size_t n,int flags){(void)flags;if(alloc_fail)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void kfree(void*p){assert(p);live--;free(p);}
static void queue_work(void*q,struct work_struct*w){(void)q;w->pending=1;}
static int btpower_on(int r){requests[delivered++]=r;return r+100;}
static int btpower_off(int r){requests[delivered++]=r;return r+100;}
static int btpower_retenion(int r){requests[delivered++]=r;return r+100;}
static int btpower_access_ctrl(int r){requests[delivered++]=r;return r+100;}
static void bt_power_vote(struct work_struct*);
static long wait_for_completion_interruptible_timeout(struct completion*c,int ms){assert(ms==600);if(wait_mode)return wait_mode==1?0:-EINTR;bt_power_vote(&pdata.wq_pwr_voting);assert(c->done);return 1;}
#define msecs_to_jiffies(n) (n)
/* ENUMS */
/* REQUEST */
/* FUNCTIONS */
int main(void){pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK));assert(!pthread_mutex_destroy(&btpower_operation_lock));assert(!pthread_mutex_init(&btpower_operation_lock,&attr));INIT_LIST_HEAD(&pdata.request_queue);int cases=0;
 for(int i=0;i<BT_MAX_PWR_STATE;i++){assert(schedule_client_voting(i)==i+100);assert(!live);assert(requests[i]==i);cases++;}
 assert(schedule_client_voting(-1)==-EINVAL);assert(schedule_client_voting(BT_MAX_PWR_STATE)==-EINVAL);cases+=2;
 alloc_fail=1;assert(schedule_client_voting(POWER_ON_BT)==-ENOMEM&&!live);alloc_fail=0;cases++;
 delivered=0;wait_mode=1;for(int i=0;i<64;i++){assert(schedule_client_voting(i%BT_MAX_PWR_STATE)==-ETIMEDOUT);assert(live==i+1);cases++;}
 assert(schedule_client_voting(0)==-ENOSPC&&live==64);cases++;bt_power_vote(&pdata.wq_pwr_voting);assert(!live&&!pdata.queued_requests&&delivered==64);for(int i=0;i<64;i++)assert(requests[i]==i%BT_MAX_PWR_STATE);
 wait_mode=2;assert(schedule_client_voting(POWER_OFF_BT)==-EINTR&&live==1);bt_power_vote(&pdata.wq_pwr_voting);assert(!live);cases++;
 printf("{\"request_results_timeouts_queue_bound_cases\":%d}\n",cases);}
