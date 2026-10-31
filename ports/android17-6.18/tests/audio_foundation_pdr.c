#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define AUDIO_PDR_DOMAIN_MAX 1
#define SERVREG_NAME_LENGTH 64
#define IS_ERR(p) ((intptr_t)(p)<0&&(intptr_t)(p)>-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define __init
#define __exit
#define pr_err(...) ((void)0)
struct mutex { pthread_mutex_t lock; };
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
struct pdr_handle { int valid; };
static int live,alloc_failure,lookup_failure,lookups,releases;
static bool test_release_reentry;
static void callback(int state,char *name,void *data){(void)state;(void)name;(void)data;}
static void *pdr_handle_alloc(void (*cb)(int,char *,void *),void *data){(void)data;assert(cb);if(alloc_failure)return alloc_failure==1?NULL:ERR_PTR(-ENOMEM);struct pdr_handle *p=malloc(sizeof(*p));assert(p);p->valid=1;live++;return p;}
static void *pdr_add_lookup(void *ptr,const char *name,const char *path){assert(ptr&&!IS_ERR(ptr));assert(!strcmp(name,"avs/audio")&&!strcmp(path,"msm/adsp/audio_pd"));lookups++;return lookup_failure?(lookup_failure==1?NULL:ERR_PTR(-EIO)):ptr;}
static void pdr_handle_release(void *ptr);
/* SOURCE */
static void pdr_handle_release(void *ptr){assert(ptr&&!IS_ERR(ptr));assert(((struct pdr_handle *)ptr)->valid);if(test_release_reentry){assert(IS_ERR(audio_pdr_service_register(0,callback)));assert(audio_pdr_service_deregister(0)==-EBUSY);}free(ptr);live--;releases++;}
int main(int argc,char **argv){assert(argc==2);unsigned cases=0;
 if(!strcmp(argv[1],"faults")){
  for(int a=0;a<=2;a++)for(int l=0;l<=2;l++){
   alloc_failure=a;lookup_failure=l;int before=lookups;void *p=audio_pdr_service_register(0,callback);
   if(a||l){assert(IS_ERR(p)&&!live&&!audio_pdr_services[0].pdr_handle);assert(lookups-before==!a);}
   else {assert(!IS_ERR_OR_NULL(p)&&live==1);assert(!audio_pdr_service_deregister(0)&&!live);}
   cases++;
  }
 }else if(!strcmp(argv[1],"lifecycle")){
  for(int i=0;i<1000;i++){
   void *p=audio_pdr_service_register(0,callback);assert(!IS_ERR_OR_NULL(p)&&live==1);
   assert(IS_ERR(audio_pdr_service_register(0,callback))&&live==1);
   test_release_reentry=true;assert(!audio_pdr_service_deregister(0));test_release_reentry=false;
   assert(!audio_pdr_service_deregister(0)&&!live);cases++;
  }
  assert(!IS_ERR(audio_pdr_service_register(0,callback)));audio_pdr_late_exit();assert(!live);cases++;
 }else if(!strcmp(argv[1],"bounds")){
  for(int d=-20;d<=20;d++)if(d){assert(IS_ERR(audio_pdr_service_register(d,callback)));assert(audio_pdr_service_deregister(d)==-EINVAL);cases++;}
  assert(IS_ERR(audio_pdr_service_register(0,NULL)));cases++;
 }else abort();
 assert(!live);printf("{\"pdr_%s_cases\":%u}\n",argv[1],cases);
}
