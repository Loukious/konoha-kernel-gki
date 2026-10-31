#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct mutex {pthread_mutex_t m;};
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->m,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->m));}
static int mutex_lock_interruptible(struct mutex *m){mutex_lock(m);return 0;}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->m));}
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define DEFINE_STATIC_SRCU(n) static pthread_rwlock_t n=PTHREAD_RWLOCK_INITIALIZER
#define __rcu
#define srcu_dereference(p,s) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_access_pointer(p) srcu_dereference(p,NULL)
#define rcu_dereference_protected(p,c) rcu_access_pointer(p)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
static int srcu_read_lock(pthread_rwlock_t *s){assert(!pthread_rwlock_rdlock(s));return 0;}
static void srcu_read_unlock(pthread_rwlock_t *s,int i){(void)i;assert(!pthread_rwlock_unlock(s));}
static void synchronize_srcu(pthread_rwlock_t *s){assert(!pthread_rwlock_wrlock(s));assert(!pthread_rwlock_unlock(s));}
#define min(a,b) ((a)<(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define GFP_KERNEL 0
static int fail_alloc,live;
static void *kzalloc(size_t n,int f){(void)f;if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void kfree(void *p){assert(live==1);live--;free(p);}
typedef struct {pthread_mutex_t lock;pthread_cond_t cond;int waiting;} wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t *w){assert(!pthread_mutex_init(&w->lock,NULL));assert(!pthread_cond_init(&w->cond,NULL));w->waiting=0;}
#define wait_event_interruptible(w,predicate) ({int r=0;assert(!pthread_mutex_lock(&(w).lock));while(!(predicate)){__atomic_add_fetch(&(w).waiting,1,__ATOMIC_RELEASE);assert(!pthread_cond_wait(&(w).cond,&(w).lock));__atomic_sub_fetch(&(w).waiting,1,__ATOMIC_RELEASE);}assert(!pthread_mutex_unlock(&(w).lock));r;})
static void wake_up_interruptible_all(wait_queue_head_t *w){assert(!pthread_mutex_lock(&w->lock));assert(!pthread_cond_broadcast(&w->cond));assert(!pthread_mutex_unlock(&w->lock));}
/* SOURCE */
struct writing {int count,result;char data[BUFFER_SIZE];};
static void *writer(void *arg){struct writing *w=arg;w->result=write_rb(w->data,w->count);return NULL;}
static void await_writer(void){for(int n=0;n<1000;n++){if(__atomic_load_n(&grb->wait_q.waiting,__ATOMIC_ACQUIRE))return;usleep(1000);}assert(0);}
static void *reader(void *arg){unsigned int *total=arg;char buffer[73];unsigned int count=0;while(count<65536){int n=read_rb(buffer,sizeof(buffer));assert(n>=0&&n<=(int)sizeof(buffer));for(int i=0;i<n;i++)assert((unsigned char)buffer[i]==(unsigned char)(count+i));count+=n;}*total=count;return NULL;}
int main(void){
 alarm(25);char in[BUFFER_SIZE],out[BUFFER_SIZE];for(int n=0;n<BUFFER_SIZE;n++)in[n]=n;assert(write_rb(in,1)==-ENODEV&&read_rb(out,1)==0&&rb_shoule_exit());fail_alloc=1;assert(create_rb()==-ENOMEM&&!live&&!grb);fail_alloc=0;assert(!create_rb()&&live==1&&create_rb()==-EBUSY);assert(write_rb(NULL,1)==-EINVAL&&write_rb(in,0)==-EINVAL&&write_rb(in,-1)==-EINVAL&&write_rb(in,BUFFER_SIZE+1)==-EINVAL&&read_rb(out,-1)==-EINVAL);
 assert(!read_rb(out,10));memset(out,0x55,sizeof(out));assert(write_rb(in,3)==3&&read_rb(out,10)==3&&!memcmp(in,out,3)&&out[3]==0x55);int wraps=0;for(int n=0;n<300;n++){int size=1+(n*317)%BUFFER_SIZE;assert(write_rb(in,size)==size&&get_rb_avalible_size()==size&&get_rb_free_size()==BUFFER_SIZE-size);assert(read_rb(out,size)==size&&!memcmp(in,out,size));wraps++;}
 assert(write_rb(in,BUFFER_SIZE)==BUFFER_SIZE&&!get_rb_free_size());struct writing w={.count=1};w.data[0]=99;pthread_t t;assert(!pthread_create(&t,NULL,writer,&w));await_writer();assert(read_rb(out,1)==1);assert(!pthread_join(t,NULL)&&w.result==1);assert(read_rb(out,BUFFER_SIZE)==BUFFER_SIZE&&out[BUFFER_SIZE-1]==99);
 assert(write_rb(in,BUFFER_SIZE)==BUFFER_SIZE);assert(!pthread_create(&t,NULL,writer,&w));await_writer();rb_init();assert(!pthread_join(t,NULL)&&w.result==-ECANCELED&&!get_rb_avalible_size());rb_end();assert(write_rb(in,1)==-ESHUTDOWN&&rb_shoule_exit());rb_init();assert(!rb_shoule_exit());rb_force_exit();assert(write_rb(in,1)==-ESHUTDOWN&&read_rb(out,1)==0);rb_init();assert(write_rb(in,BUFFER_SIZE)==BUFFER_SIZE);assert(!pthread_create(&t,NULL,writer,&w));await_writer();assert(!release_rb()&&!pthread_join(t,NULL)&&w.result==-ESHUTDOWN&&!live&&!grb);assert(!release_rb());
 assert(!create_rb());unsigned int total=0;pthread_t rd;assert(!pthread_create(&rd,NULL,reader,&total));unsigned int count=0;while(count<65536){int size=min(113,65536-count);for(int i=0;i<size;i++)in[i]=count+i;assert(write_rb(in,size)==size);count+=size;}assert(!pthread_join(rd,NULL)&&total==65536&&!get_rb_avalible_size());assert(!release_rb()&&!live);
 printf("{\"allocation_faults\":1,\"wraparound_cases\":%d,\"concurrent_fifo_bytes\":%u,\"short_reads_return_actual_bytes\":true,\"reset_cancels_blocked_writer\":true,\"removal_cancels_blocked_writer\":true,\"bounds_and_duplicate_instance\":true}\n",wraps,total);
}
