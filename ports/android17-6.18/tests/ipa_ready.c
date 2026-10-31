#define IPA_REAL_MUTEX
#include "ipa_mock.h"
#include <stdatomic.h>
#include <sched.h>
#define IPADBG(...) ((void)0)
#define IPAERR(...) ((void)0)
typedef void (*ipa_ready_cb)(void*);
struct ipa_ready_cb_info{struct list_head link;ipa_ready_cb ready_cb;void*user_data;};
struct ipa3_context{struct mutex lock;bool ipa_initialization_complete;struct list_head ipa_ready_cb_list;};
static struct ipa3_context*ipa3_ctx;
static DEFINE_MUTEX(ipa_ready_notify_lock);
static atomic_bool entered,release_callback,unregister_started,unregister_done;
static int calls,other_calls;
static void callback(void*p){assert(p==(void*)1);calls++;atomic_store(&entered,true);while(!atomic_load(&release_callback))sched_yield();}
static void other(void*p){other_calls++;}
/* FUNCTIONS */
static void*trigger_thread(void*p){ipa_trigger_ipa_ready_cbs();return NULL;}
static void*unregister_thread(void*p){atomic_store(&unregister_started,true);ipa3_unregister_ipa_ready_cb(callback,(void*)1);atomic_store(&unregister_done,true);return NULL;}
int main(void)
{
 struct ipa3_context ctx={0};ipa3_ctx=&ctx;mutex_init(&ctx.lock);INIT_LIST_HEAD(&ctx.ipa_ready_cb_list);
 alloc_fault=1;assert(ipa_register_ipa_ready_cb(callback,(void*)1)==-ENOMEM);alloc_fault=0;
 assert(!ipa_register_ipa_ready_cb(other,(void*)2));ipa3_unregister_ipa_ready_cb(other,(void*)3);assert(!list_empty(&ctx.ipa_ready_cb_list));ipa3_unregister_ipa_ready_cb(other,(void*)2);assert(list_empty(&ctx.ipa_ready_cb_list)&&!live);
 assert(!ipa_register_ipa_ready_cb(callback,(void*)1));pthread_t a,b;assert(!pthread_create(&a,NULL,trigger_thread,NULL));while(!atomic_load(&entered))sched_yield();assert(!pthread_create(&b,NULL,unregister_thread,NULL));while(!atomic_load(&unregister_started))sched_yield();assert(!atomic_load(&unregister_done));
 /* Registration uses the context lock and remains possible during a callback. */
 assert(!ipa_register_ipa_ready_cb(other,(void*)2));atomic_store(&release_callback,true);assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(calls==1&&!other_calls);ipa_trigger_ipa_ready_cbs();assert(other_calls==1&&!live);
 ctx.ipa_initialization_complete=true;assert(ipa_register_ipa_ready_cb(other,NULL)==-EEXIST);
 puts("{\"ready_allocation_failure_safe\":true,\"unregister_waits_for_dispatch\":true,\"register_from_dispatch_lock_safe\":true,\"completed_registration_contract_preserved\":true}");
}
