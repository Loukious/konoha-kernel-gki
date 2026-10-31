#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
typedef uint32_t u32;typedef void* smmu_proxy_map_sgtable;typedef void* smmu_proxy_unmap_sgtable;
#define CAT2(a,b) a##b
#define CAT(a,b) CAT2(a,b)
static pthread_mutex_t *locked(pthread_mutex_t *p){pthread_mutex_lock(p);return p;}
static void unlocked(pthread_mutex_t **p){pthread_mutex_unlock(*p);}
#define guard(x) pthread_mutex_t *CAT(lock_,__LINE__) __attribute__((cleanup(unlocked)))=locked
#define IS_ERR(p) ((intptr_t)(p)<0)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define GH_MSGQ_LABEL_SMMU_PROXY 7
#define THIS_MODULE ((void*)1)
#define CTrustedCameraDriver_UID 3
struct csf_version {u32 arch_ver,max_ver,min_ver;};
struct Object {int id;};
#define Object_NULL ((struct Object){0})
struct platform_device {int unused;};
struct smmu_proxy_resp_hdr {u32 msg_type,msg_size;int ret;};
static struct csf_version cached_csf_version,firmware_version={2,5,1};
static pthread_mutex_t sender_mutex=PTHREAD_MUTEX_INITIALIZER,csf_version_lock=PTHREAD_MUTEX_INITIALIZER;
static void *msgq_hdl;
static int env_error,open_error,version_error,held,version_calls;
static int get_client_env_object(struct Object *o){if(env_error)return env_error;o->id=1;held++;return 0;}
static int IClientEnv_open(struct Object env,int id,struct Object *o){assert(env.id==1&&id==3);if(open_error)return open_error;o->id=2;held++;return 0;}
static int ITrustedCameraDriver_getVersion(struct Object o,u32 *a,u32 *b,u32 *c){assert(o.id==2);version_calls++;if(version_error)return version_error;*a=firmware_version.arch_ver;*b=firmware_version.max_ver;*c=firmware_version.min_ver;return 0;}
static void Object_release(struct Object o){assert(o.id&&held);held--;}
static int fault_step,steps,queue_live,dev_live,callbacks,refs;
static int fault(void){return ++steps==fault_step;}
static void *gh_msgq_register(int label){assert(label==7);if(fault())return (void*)(intptr_t)-ENOMEM;queue_live++;return (void*)8;}
static void gh_msgq_unregister(void *h){assert(h==(void*)8&&queue_live);queue_live--;}
static int smmu_proxy_dev_fops;
static int smmu_proxy_create_dev(int *f){assert(f==&smmu_proxy_dev_fops);if(fault())return -EIO;dev_live++;return 0;}
static void smmu_proxy_destroy_dev(void){assert(dev_live);dev_live--;}
static void smmu_proxy_map(void) {}
static void smmu_proxy_unmap_nop(void) {}
int smmu_proxy_get_csf_version(struct csf_version *version);
static int qti_smmu_proxy_register_callbacks(void *map,void *unmap){assert(dev_live&&queue_live);assert(map&&unmap);if(fault())return -EBUSY;callbacks++;return 0;}
static void __module_get(void *m){assert(m==THIS_MODULE);refs++;}
/* FUNCTIONS */
int main(void) {
 unsigned cases=0;struct smmu_proxy_resp_hdr h;for(unsigned size=0;size<128;size++)for(unsigned declared=0;declared<128;declared++)for(unsigned type=0;type<8;type++){h=(struct smmu_proxy_resp_hdr){type,declared,0};assert(smmu_proxy_response_valid(&h,size,28,1)==(size==28&&declared==28&&type==1));cases++;}
 struct csf_version result={0};assert(smmu_proxy_get_csf_version(NULL)==-EINVAL);
 env_error=-EIO;assert(smmu_proxy_get_csf_version(&result)==-EIO&&!held&&!cached_csf_version.arch_ver);env_error=0;
 open_error=-EACCES;assert(smmu_proxy_get_csf_version(&result)==-EACCES&&!held&&!cached_csf_version.arch_ver);open_error=0;
 version_error=-EIO;assert(smmu_proxy_get_csf_version(&result)==-EIO&&!held&&!cached_csf_version.arch_ver);version_error=0;
 assert(!smmu_proxy_get_csf_version(&result)&&result.arch_ver==2&&result.max_ver==5&&result.min_ver==1&&!held);int calls=version_calls;firmware_version.max_ver=0;assert(!smmu_proxy_get_csf_version(&result)&&result.max_ver==5&&version_calls==calls);
 struct platform_device p;for(int f=1;f<=3;f++){fault_step=f;steps=0;assert(sender_probe_handler(&p)<0);assert(!queue_live&&!dev_live&&!callbacks&&!refs&&!msgq_hdl);}
 fault_step=0;steps=0;assert(!sender_probe_handler(&p)&&queue_live==1&&dev_live==1&&callbacks==1&&refs==1);assert(sender_probe_handler(&p)==-EBUSY);
 printf("{\"reply_bounds_cases\":%u,\"version_failure_and_cache_cases\":5,\"probe_failure_cases\":3,\"callback_publication_after_setup\":true}\n",cases);
}
