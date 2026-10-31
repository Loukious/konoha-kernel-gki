#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#define EPROBE_DEFER 517
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELEASE)
#define mutex_lock(p) assert(!pthread_mutex_lock(p))
#define mutex_unlock(p) assert(!pthread_mutex_unlock(p))
struct file{void*private_data;};struct inode{int unused;};struct device{int refs;};
struct platform_pwr_data{pthread_mutex_t pwr_mtx;struct device*slim_dev;};
static struct platform_pwr_data data={.pwr_mtx=PTHREAD_MUTEX_INITIALIZER},*pwr_data=&data;
static pthread_mutex_t btpower_ioctl_lock=PTHREAD_MUTEX_INITIALIZER,btpower_operation_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t btpower_srcu=PTHREAD_RWLOCK_INITIALIZER;
static int srcu_read_lock(pthread_rwlock_t*l){assert(!pthread_rwlock_rdlock(l));return 0;}
static void srcu_read_unlock(pthread_rwlock_t*l,int i){assert(!i);assert(!pthread_rwlock_unlock(l));}
static void synchronize_srcu(pthread_rwlock_t*l){assert(!pthread_rwlock_wrlock(l));assert(!pthread_rwlock_unlock(l));}
static struct device*get_device(struct device*d){if(d){assert(d->refs>0);d->refs++;}return d;}
static void put_device(struct device*d){if(d)assert(--d->refs>=0);}
static bool probe_finished;static unsigned long btpower_generation;
enum{BT_CMD_PWR_CTRL=1,UWB_CMD_PWR_CTRL,BT_CMD_ACCESS_CTRL,UWB_CMD_ACCESS_CTRL,OTHER};
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;static int block_call,entered,release_call,retired,dispatch_calls;
static long bt_ioctl_dispatch(struct file*f,unsigned int cmd,unsigned long arg){(void)f;assert(pwr_data==&data);dispatch_calls++;pthread_mutex_lock(&gate);if(block_call){entered=1;pthread_cond_broadcast(&cond);while(!release_call)pthread_cond_wait(&cond,&gate);}pthread_mutex_unlock(&gate);assert(pwr_data==&data);return cmd+arg;}
/* FUNCTIONS */
static int setter_started;
static void*setter(void*d){WRITE_ONCE(setter_started,1);assert(!btpower_register_slimdev(d));return NULL;}
static void*caller(void*f){assert(bt_ioctl(f,OTHER,10)==OTHER+10);return NULL;}
static void*remover(void*u){(void)u;WRITE_ONCE(probe_finished,false);synchronize_srcu(&btpower_srcu);pwr_data=NULL;WRITE_ONCE(retired,1);return NULL;}
int main(void){pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK));assert(!pthread_mutex_destroy(&btpower_operation_lock));assert(!pthread_mutex_init(&btpower_operation_lock,&attr));struct inode inode={0};struct file old={0},next={0};struct device a={.refs=1},b={.refs=1};int cases=0;
 assert(bt_open(&inode,&old)==-EAGAIN);assert(btpower_register_slimdev(&a)==-EPROBE_DEFER&&a.refs==1);cases+=2;
 btpower_generation=1;WRITE_ONCE(probe_finished,true);assert(!bt_open(&inode,&old));assert(bt_ioctl(&old,OTHER,4)==OTHER+4);cases+=2;
 assert(!btpower_register_slimdev(&a)&&a.refs==2);assert(!btpower_register_slimdev(&b)&&a.refs==1&&b.refs==2);assert(btpower_register_slimdev(NULL)==-EPROBE_DEFER);cases+=3;
 pthread_t replacement;mutex_lock(&btpower_operation_lock);assert(!pthread_create(&replacement,NULL,setter,&a));while(!READ_ONCE(setter_started))usleep(1000);usleep(10000);assert(a.refs==1&&b.refs==2);mutex_unlock(&btpower_operation_lock);assert(!pthread_join(replacement,NULL));assert(a.refs==2&&b.refs==1);cases++;
 block_call=1;pthread_t t,r;assert(!pthread_create(&t,NULL,caller,&old));pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);assert(!pthread_create(&r,NULL,remover,NULL));while(READ_ONCE(probe_finished))usleep(1000);usleep(10000);assert(!READ_ONCE(retired));assert(bt_ioctl(&old,OTHER,4)==-ENODEV);pthread_mutex_lock(&gate);release_call=1;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&gate);assert(!pthread_join(t,NULL)&&!pthread_join(r,NULL));assert(retired);cases+=3;
 pwr_data=&data;btpower_generation=2;WRITE_ONCE(probe_finished,true);assert(bt_ioctl(&old,OTHER,4)==-ENODEV);assert(!bt_open(&inode,&next));block_call=0;assert(bt_ioctl(&next,BT_CMD_PWR_CTRL,10)==BT_CMD_PWR_CTRL+10);cases+=3;
 put_device(data.slim_dev);data.slim_dev=NULL;assert(b.refs==1&&dispatch_calls==3);printf("{\"admission_rebind_provider_and_concurrent_withdrawal_cases\":%d}\n",cases);}
