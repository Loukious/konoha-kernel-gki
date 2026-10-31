#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdatomic.h>
#include <unistd.h>
typedef uint64_t u64; typedef uint32_t u32; typedef int64_t s64;
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define EXPORT_SYMBOL_GPL(x)
#define BIT(n) (1ULL << (n))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define READ_ONCE(x) (x)
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x) || IS_ERR(x))
#define PTR_ERR(x) ((long)(x))
#define ERR_PTR(x) ((void *)(long)(x))
#define __user
#define EPROBE_DEFER 517
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
static void list_add_tail(struct list_head *n,struct list_head *h) { n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n; }
static void list_del_init(struct list_head *n) { n->prev->next=n->next;n->next->prev=n->prev;INIT_LIST_HEAD(n); }
static bool list_empty(struct list_head *h) { return h->next==h; }
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
#define list_for_each_entry(p,h,m) for(p=list_first_entry(h,__typeof__(*p),m);&p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
typedef pthread_mutex_t spinlock_t;
static void spin_lock_init(spinlock_t *l) { assert(!pthread_mutex_init(l,NULL)); }
static void spin_lock(spinlock_t *l) { assert(!pthread_mutex_lock(l)); }
static void spin_unlock(spinlock_t *l) { assert(!pthread_mutex_unlock(l)); }
#define spin_lock_irqsave(l,f) ((f)=0,spin_lock(l))
#define spin_unlock_irqrestore(l,f) ((void)(f),spin_unlock(l))
#define spin_lock_irq(l) spin_lock(l)
#define spin_unlock_irq(l) spin_unlock(l)
typedef atomic_int atomic_t;
#define atomic_read(v) atomic_load(v)
#define atomic_set(v,n) atomic_store(v,n)
#define atomic_inc(v) atomic_fetch_add(v,1)
#define atomic_dec(v) atomic_fetch_sub(v,1)
#define atomic_dec_and_test(v) (atomic_fetch_sub(v,1)==1)
struct completion { pthread_mutex_t lock;pthread_cond_t cond;bool done; };
static void init_completion(struct completion *c) { assert(!pthread_mutex_init(&c->lock,NULL)); assert(!pthread_cond_init(&c->cond,NULL)); c->done=false; }
static void reinit_completion(struct completion *c) { pthread_mutex_lock(&c->lock);c->done=false;pthread_mutex_unlock(&c->lock); }
static void complete_all(struct completion *c) { pthread_mutex_lock(&c->lock);c->done=true;pthread_cond_broadcast(&c->cond);pthread_mutex_unlock(&c->lock); }
static void wait_for_completion(struct completion *c) { pthread_mutex_lock(&c->lock);while(!c->done)pthread_cond_wait(&c->cond,&c->lock);pthread_mutex_unlock(&c->lock); }
struct task_struct;
#define current ((struct task_struct *)(uintptr_t)pthread_self())
static atomic_int allocations;
static int fail_alloc;
static void *kzalloc(size_t n,int flags) { (void)flags;if(fail_alloc){fail_alloc=0;return NULL;} void *p=calloc(1,n);assert(p);atomic_fetch_add(&allocations,1);return p; }
static void kfree(void *p) { if(p){atomic_fetch_sub(&allocations,1);free(p);} }
