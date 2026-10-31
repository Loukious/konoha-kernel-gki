#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CLD80211_MAX_COMMANDS 40
#define CLD80211_ATTR_VENDOR_DATA 1
#define GFP_KERNEL 0
typedef uint8_t u8;
typedef void (*cld80211_cb)(const void *,int,void *,int);
struct cld_ops {cld80211_cb cb;void *cb_ctx;};
struct {struct cld_ops *cld_ops[40];} nl_data;
static pthread_mutex_t cld_callback_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t cld_callback_srcu=PTHREAD_RWLOCK_INITIALIZER;
static bool cld_ready=true;
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define rcu_access_pointer(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),v,__ATOMIC_RELEASE)
#define RCU_INIT_POINTER(p,v) rcu_assign_pointer(p,v)
#define rcu_dereference_protected(p,c) rcu_access_pointer(p)
#define srcu_dereference(p,s) rcu_access_pointer(p)
static int fail_alloc,allocations;
static void *kmalloc(size_t n,int flags){(void)flags;if(fail_alloc)return NULL;void *p=malloc(n);assert(p);allocations++;return p;}
static void kfree(void *p){if(p)allocations--;free(p);}
static int srcu_read_lock(pthread_rwlock_t *p){pthread_rwlock_rdlock(p);return 0;}
static void srcu_read_unlock(pthread_rwlock_t *p,int idx){(void)idx;pthread_rwlock_unlock(p);}
static void synchronize_srcu(pthread_rwlock_t *p){pthread_rwlock_wrlock(p);pthread_rwlock_unlock(p);}
struct sk_buff {int unused;};
struct genl_split_ops {u8 cmd;};
struct genlmsghdr {u8 cmd;};
struct nlattr {int len;char bytes[32];};
struct genl_info {struct genlmsghdr *genlhdr;struct nlattr *attrs[2];unsigned snd_portid;};
static void *nla_data(struct nlattr *p){return p->bytes;}
static int nla_len(struct nlattr *p){return p->len;}
/* FUNCTIONS */
static int calls;
static void callback(const void *p,int len,void *ctx,int pid){assert(ctx==&calls&&p&&len==5&&pid==19);calls++;}
static pthread_mutex_t test_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond=PTHREAD_COND_INITIALIZER;
static bool entered,release_callback,withdraw_started,withdraw_done;
static void blocking(const void *p,int len,void *ctx,int pid){
 assert(p&&len==5&&ctx==&calls&&pid==19);
 assert(!register_cld_cmd_cb(2,callback,&calls));
 pthread_mutex_lock(&test_lock);entered=true;pthread_cond_broadcast(&test_cond);
 while(!release_callback)pthread_cond_wait(&test_cond,&test_lock);
 pthread_mutex_unlock(&test_lock);calls++;
}
static void *invoke(void *arg){assert(!cld80211_doit(NULL,arg));return NULL;}
static void *withdraw(void *arg){(void)arg;pthread_mutex_lock(&test_lock);withdraw_started=true;pthread_cond_broadcast(&test_cond);pthread_mutex_unlock(&test_lock);assert(!deregister_cld_cmd_cb(1));pthread_mutex_lock(&test_lock);withdraw_done=true;pthread_cond_broadcast(&test_cond);pthread_mutex_unlock(&test_lock);return NULL;}
int main(void){
 int bounds=0;
 for(int cmd=0;cmd<256;cmd++){
  struct genl_split_ops op={.cmd=cmd};int ret=cld80211_pre_doit(&op,NULL,NULL);
  assert(ret==((cmd>=1&&cmd<=40)?0:-EOPNOTSUPP));
  ret=register_cld_cmd_cb(cmd,callback,&calls);
  assert(ret==((cmd>=1&&cmd<=40)?0:-EINVAL));
  if(!ret){assert(register_cld_cmd_cb(cmd,callback,&calls)==-EEXIST);assert(!deregister_cld_cmd_cb(cmd));}
  assert(deregister_cld_cmd_cb(cmd)==((cmd>=1&&cmd<=40)?0:-EINVAL));bounds++;
 }
 assert(register_cld_cmd_cb(1,NULL,NULL)==-EINVAL);
 fail_alloc=1;assert(register_cld_cmd_cb(1,callback,&calls)==-ENOMEM);fail_alloc=0;
 cld_ready=false;assert(register_cld_cmd_cb(1,callback,&calls)==-ENODEV);cld_ready=true;
 struct nlattr a={.len=5,.bytes="hello"};struct genlmsghdr h={.cmd=1};struct genl_info i={.genlhdr=&h,.attrs={NULL,&a},.snd_portid=19};
 assert(cld80211_doit(NULL,&i)==-EOPNOTSUPP);assert(!register_cld_cmd_cb(1,callback,&calls));assert(!cld80211_doit(NULL,&i)&&calls==1);
 i.attrs[1]=NULL;assert(cld80211_doit(NULL,&i)==-EINVAL);i.attrs[1]=&a;assert(!deregister_cld_cmd_cb(1));
 for(int round=0;round<200;round++){
  entered=release_callback=withdraw_started=withdraw_done=false;
  assert(!register_cld_cmd_cb(1,blocking,&calls));pthread_t reader,remover;assert(!pthread_create(&reader,NULL,invoke,&i));
  pthread_mutex_lock(&test_lock);while(!entered)pthread_cond_wait(&test_cond,&test_lock);pthread_mutex_unlock(&test_lock);
  assert(!pthread_create(&remover,NULL,withdraw,NULL));pthread_mutex_lock(&test_lock);while(!withdraw_started)pthread_cond_wait(&test_cond,&test_lock);assert(!withdraw_done);release_callback=true;pthread_cond_broadcast(&test_cond);pthread_mutex_unlock(&test_lock);
  pthread_join(reader,NULL);pthread_join(remover,NULL);assert(withdraw_done);assert(!deregister_cld_cmd_cb(2));assert(cld80211_doit(NULL,&i)==-EOPNOTSUPP);
 }
 assert(!allocations&&calls==201);
 puts("{\"command_bounds\":256,\"duplicate_and_allocation_errors\":true,\"callback_context_pair\":true,\"threaded_unregister_drain\":200,\"registration_reentry\":true}");
}
