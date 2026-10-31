#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <stdalign.h>
#include <time.h>
#define GFP_KERNEL 0
#define EXPORT_SYMBOL(x)
#define DEFINE_MUTEX(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define pr_err(...) ((void)0)
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n = {&n,&n}
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry_rcu(p,h,m,...) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of((p)->m.next,__typeof__(*p),m);&(p)->m!=(h);p=t,t=container_of((t)->m.next,__typeof__(*t),m))
static void list_add_rcu(struct list_head *n,struct list_head *h){n->next=h->next;n->prev=h;h->next->prev=n;h->next=n;}
static void list_del_rcu(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;}
static bool list_empty(struct list_head *h){return h->next==h;}
struct srcu_struct {pthread_mutex_t lock;pthread_cond_t cond;int readers;};
#define DEFINE_STATIC_SRCU(n) static struct srcu_struct n={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond=PTHREAD_COND_INITIALIZER;
static bool reader_entered,reader_may_leave,removal_started,removal_done;
static int srcu_read_lock(struct srcu_struct *s){pthread_mutex_lock(&s->lock);++s->readers;pthread_mutex_unlock(&s->lock);return 0;}
static void srcu_read_unlock(struct srcu_struct *s,int i){(void)i;pthread_mutex_lock(&s->lock);--s->readers;pthread_cond_broadcast(&s->cond);pthread_mutex_unlock(&s->lock);}
static bool srcu_read_lock_held(struct srcu_struct *s){return s->readers>0;}
static void synchronize_srcu(struct srcu_struct *s){pthread_mutex_lock(&s->lock);while(s->readers)pthread_cond_wait(&s->cond,&s->lock);pthread_mutex_unlock(&s->lock);}
struct clk_hw {int sentinel;};struct seq_file {int ignored;};struct device;
struct resource {void (*release)(struct device *,void *);max_align_t align;unsigned char payload[];};
struct device {struct resource *r[32];int nr;};
static int alloc_fail;
static void *devres_alloc(void (*release)(struct device *,void *),size_t n,int flags){struct resource *r;(void)flags;if(alloc_fail)return NULL;r=calloc(1,sizeof(*r)+n);assert(r);r->release=release;return r->payload;}
static void devres_add(struct device *d,void *p){d->r[d->nr++]=container_of(p,struct resource,payload);}
static void release_all(struct device *d){while(d->nr){struct resource *r=d->r[--d->nr];r->release(d,r->payload);free(r);}}
#define clock_debug_output(...) ((void)0)
#include "debug_decl.inc"
static int clock_debug_print_clock(struct hw_debug_clk *d,struct seq_file *s){(void)s;assert(d->clk_hw->sentinel==123);pthread_mutex_lock(&gate);reader_entered=true;pthread_cond_broadcast(&gate_cond);while(!reader_may_leave)pthread_cond_wait(&gate_cond,&gate);pthread_mutex_unlock(&gate);assert(d->clk_hw->sentinel==123);return 1;}
#include "debug.inc"
static void *read_thread(void *p){(void)p;clock_debug_print_enabled_clocks(NULL);return NULL;}
static void *remove_thread(void *p){pthread_mutex_lock(&gate);removal_started=true;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate);release_all(p);pthread_mutex_lock(&gate);removal_done=true;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate);return NULL;}
int main(void){struct device d={0},other={0};struct clk_hw hw={123},hw2={123};pthread_t reader,remover;struct timespec deadline;int ret;
 alloc_fail=1;assert(clk_hw_debug_register(&d,&hw)==-ENOMEM);assert(list_empty(&clk_hw_debug_list));alloc_fail=0;
 assert(clk_hw_debug_register(&d,&hw)==0);assert(pthread_create(&reader,NULL,read_thread,NULL)==0);pthread_mutex_lock(&gate);while(!reader_entered)pthread_cond_wait(&gate_cond,&gate);pthread_mutex_unlock(&gate);
 assert(pthread_create(&remover,NULL,remove_thread,&d)==0);pthread_mutex_lock(&gate);while(!removal_started)pthread_cond_wait(&gate_cond,&gate);timespec_get(&deadline,TIME_UTC);deadline.tv_nsec+=50000000;if(deadline.tv_nsec>=1000000000){++deadline.tv_sec;deadline.tv_nsec-=1000000000;}ret=0;while(!removal_done&&ret!=ETIMEDOUT)ret=pthread_cond_timedwait(&gate_cond,&gate,&deadline);assert(!removal_done);reader_may_leave=true;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate);
 pthread_join(reader,NULL);pthread_join(remover,NULL);assert(removal_done&&list_empty(&clk_hw_debug_list));
 assert(clk_hw_debug_register(&d,&hw)==0);assert(clk_hw_debug_register(&other,&hw2)==0);release_all(&d);assert(!list_empty(&clk_hw_debug_list));clk_debug_unregister();assert(list_empty(&clk_hw_debug_list));release_all(&other);puts("PASS debug: allocation failure, reader/remove synchronization, per-device ownership and global detach");return 0;}
