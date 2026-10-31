#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#define CAT2(a,b) a##b
#define CAT(a,b) CAT2(a,b)
static pthread_mutex_t *locked(pthread_mutex_t *p){pthread_mutex_lock(p);return p;}
static void unlocked(pthread_mutex_t **p){pthread_mutex_unlock(*p);}
#define guard(x) pthread_mutex_t *CAT(lock_,__LINE__) __attribute__((cleanup(unlocked)))=locked
#define smp_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#define smp_load_acquire(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define WRITE_ONCE(x,y) __atomic_store_n(&(x),(y),__ATOMIC_RELAXED)
typedef int (*smmu_proxy_map_sgtable)(void);
typedef void (*smmu_proxy_unmap_sgtable)(void);
struct smmu_proxy_callbacks {smmu_proxy_map_sgtable map_sgtable;smmu_proxy_unmap_sgtable unmap_sgtable;};
static int invalid(void){return -EINVAL;}
static void invalid_unmap(void){}
static int map(void){return 0;}
static void unmap(void){}
static struct smmu_proxy_callbacks smmu_proxy_callback_ops={invalid,invalid_unmap};
static pthread_mutex_t smmu_proxy_ops_lock=PTHREAD_MUTEX_INITIALIZER;
static bool smmu_proxy_ops_registered;
/* FUNCTIONS */
static unsigned calls;
static void *reader(void *unused){(void)unused;for(unsigned i=0;i<100000;i++){struct smmu_proxy_callbacks ops=smmu_proxy_callbacks_get();if(ops.map_sgtable==map)assert(ops.unmap_sgtable==unmap);__atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);}return NULL;}
int main(void){assert(qti_smmu_proxy_register_callbacks(map,NULL)==-EINVAL);assert(qti_smmu_proxy_register_callbacks(NULL,unmap)==-EINVAL);pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,reader,NULL));assert(!qti_smmu_proxy_register_callbacks(map,unmap));for(unsigned i=0;i<4;i++)pthread_join(threads[i],NULL);assert(qti_smmu_proxy_register_callbacks(NULL,NULL)==-EBUSY);struct smmu_proxy_callbacks ops=smmu_proxy_callbacks_get();assert(ops.map_sgtable==map&&ops.unmap_sgtable==unmap);printf("{\"concurrent_callback_snapshots\":%u,\"rejects_replacement_or_partial_pair\":true}\n",calls);}
