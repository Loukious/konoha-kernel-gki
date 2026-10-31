#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GFP_KERNEL 1
#define GFP_ATOMIC 2
#define SLAB_ACCOUNT 0
#define THIS_MODULE NULL
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define ADRASTEA_DEVICE_ID 1
#define WCN6750_DEVICE_ID 2
#define WCN7750_DEVICE_ID 3
#define WCN6450_DEVICE_ID 4
#define QCA6390_DEVICE_ID 5
#define QCA6490_DEVICE_ID 6
#define MANGO_DEVICE_ID 7
#define PEACH_DEVICE_ID 8
#define KIWI_DEVICE_ID 9
#define FIG_DEVICE_ID 10
#define pr_info(...) do {} while(0)
typedef int gfp_t;
#define pr_err(...) do {} while(0)
#define pr_debug(...) do {} while(0)
#define pr_warn(...) do {} while(0)
#define DEFINE_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define DEFINE_MUTEX(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define DEFINE_STATIC_SRCU(n) static pthread_rwlock_t n=PTHREAD_RWLOCK_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
static _Thread_local int spin_depth;
#define spin_lock_irqsave(l,f) do {(f)=0;pthread_mutex_lock(l);spin_depth++;} while(0)
#define spin_unlock_irqrestore(l,f) do {(void)(f);spin_depth--;pthread_mutex_unlock(l);} while(0)
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
static int srcu_read_lock(pthread_rwlock_t *p){pthread_rwlock_rdlock(p);return 0;}
static void srcu_read_unlock(pthread_rwlock_t *p,int idx){(void)idx;pthread_rwlock_unlock(p);}
static void synchronize_srcu(pthread_rwlock_t *p){pthread_rwlock_wrlock(p);pthread_rwlock_unlock(p);}
static atomic_int live,refs,steps;
static int fail_step,fail_growth;
static bool failure(void){int step=atomic_fetch_add(&steps,1)+1;return fail_step&&step==fail_step;}
static void *allocate(size_t n){if(failure())return NULL;void *p=malloc(n);assert(p);atomic_fetch_add(&live,1);return p;}
static void kfree(void *p){if(p){atomic_fetch_sub(&live,1);free(p);}}
static void *kcalloc(size_t n,size_t size,int flags){(void)flags;void *p=allocate(n*size);if(p)memset(p,0,n*size);return p;}
struct kmem_cache {size_t size;};
typedef struct {struct kmem_cache *cache;} mempool_t;
static struct kmem_cache *kmem_cache_create_usercopy(const char *name,size_t size,int align,int flags,size_t offset,size_t user_size,void *ctor){(void)name;(void)align;(void)flags;(void)offset;(void)user_size;(void)ctor;struct kmem_cache *p=allocate(sizeof(*p));if(p)p->size=size;return p;}
static void kmem_cache_destroy(struct kmem_cache *p){kfree(p);}
#define mempool_alloc_slab NULL
#define mempool_free_slab NULL
static mempool_t *mempool_create(int min,void *alloc,void *free_fn,struct kmem_cache *cache){(void)min;(void)alloc;(void)free_fn;mempool_t *p=allocate(sizeof(*p));if(p)p->cache=cache;return p;}
static void mempool_destroy(mempool_t *p){kfree(p);}
static void *mempool_alloc(mempool_t *p,int flags){assert(p&&!spin_depth&&(flags==GFP_KERNEL||flags==GFP_ATOMIC));void *mem=allocate(p->cache->size);if(mem)memset(mem,0xa5,p->cache->size);return mem;}
static void mempool_free(void *p,mempool_t *pool){assert(pool&&!spin_depth);kfree(p);}
static void *krealloc_array(void *old,size_t n,size_t size,int flags){assert(spin_depth&&flags==GFP_ATOMIC);if(fail_growth)return NULL;void *p=realloc(old,n*size);assert(p);return p;}
static bool in_interrupt(void){return false;}
static bool preemptible(void){return true;}
static int rcu_preempt_depth(void){return 0;}
static void __module_get(void *p){(void)p;atomic_fetch_add(&refs,1);}
static void module_put(void *p){(void)p;assert(atomic_fetch_sub(&refs,1)>0);}
/* DECLARATIONS */
/* FUNCTIONS */
static void *borrower(void *arg){(void)arg;for(int i=0;i<1000;i++){void *p=wcnss_prealloc_get(8192);if(p){assert(!((unsigned char *)p)[0]&&!((unsigned char *)p)[8191]);assert(wcnss_prealloc_put(p)==1);}}return NULL;}
int main(void){
 for(int fault=1;fault<=15;fault++){
  fail_step=fault;steps=0;cnss_initialize_prealloc_pool(WCN7750_DEVICE_ID);
  assert(!pool_storage_available&&!mempool_initialization_done&&!live&&!refs);
  assert(!wcnss_prealloc_get(8192));cnss_deinitialize_prealloc_pool();
 }
 fail_step=0;steps=0;cnss_initialize_prealloc_pool(WCN7750_DEVICE_ID);assert(pool_storage_available&&mempool_initialization_done&&steps==15);
 assert(!wcnss_prealloc_get(0)&&!wcnss_prealloc_get(8191)&&!wcnss_prealloc_get(SIZE_MAX));
 void *a=wcnss_prealloc_get(8192),*b=wcnss_prealloc_get(8192);assert(a&&b&&a!=b&&refs==2);
 void **old=cnss_pools[0].pool_ptrs;fail_growth=1;assert(!wcnss_prealloc_get(8192));assert(cnss_pools[0].pool_ptrs==old&&old[0]==a&&old[1]==b&&cnss_pools[0].table_capacity==2&&refs==2);fail_growth=0;
 void *c=wcnss_prealloc_get(8192);assert(c&&cnss_pools[0].table_capacity==3&&refs==3);
 cnss_initialize_prealloc_pool(WCN7750_DEVICE_ID);assert(refs==3);
 cnss_deinitialize_prealloc_pool();assert(pool_storage_available&&!mempool_initialization_done&&!wcnss_prealloc_get(8192));
 cnss_initialize_prealloc_pool(ADRASTEA_DEVICE_ID);assert(!mempool_initialization_done&&cnss_prealloc_pool_size==5);
 assert(wcnss_prealloc_put(a)==1);assert(wcnss_prealloc_put(a)==0);assert(wcnss_prealloc_put(NULL)==0);int foreign;assert(!wcnss_prealloc_put(&foreign));
 cnss_initialize_prealloc_pool(WCN7750_DEVICE_ID);assert(mempool_initialization_done);
 assert(wcnss_prealloc_put(b)==1&&wcnss_prealloc_put(c)==1&&!refs);cnss_deinitialize_prealloc_pool();assert(!live&&!pool_storage_available);
 cnss_initialize_prealloc_pool(WCN7750_DEVICE_ID);
 pthread_t threads[4];for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,borrower,NULL));
 for(int i=0;i<100;i++){cnss_deinitialize_prealloc_pool();cnss_initialize_prealloc_pool(WCN7750_DEVICE_ID);}
 for(int i=0;i<4;i++)pthread_join(threads[i],NULL);
 cnss_deinitialize_prealloc_pool();assert(!refs&&!live&&!pool_storage_available);
 puts("{\"initialization_faults\":15,\"tracker_failure_preserves_borrowers\":true,\"returned_buffers_zeroed\":true,\"borrowers_prevent_destroy_and_wrong_device_replacement\":true,\"double_and_foreign_returns\":true,\"concurrent_borrow_attempts\":4000,\"concurrent_lifecycle_cycles\":100}");
}
