#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
typedef uint32_t u32;
#define __rcu
#define GFP_KERNEL 0
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
#define pr_err(...) do {} while (0)
#define pr_debug(...) do {} while (0)
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define rcu_access_pointer(p) __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define srcu_dereference(p,l) rcu_access_pointer(p)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p), (v), __ATOMIC_RELEASE)
static int srcu_read_lock(pthread_rwlock_t *p) { pthread_rwlock_rdlock(p); return 0; }
static void srcu_read_unlock(pthread_rwlock_t *p, int i) { (void)i; pthread_rwlock_unlock(p); }
static atomic_int draining;
static void synchronize_srcu(pthread_rwlock_t *p) { atomic_store(&draining, 1); pthread_rwlock_wrlock(p); pthread_rwlock_unlock(p); }
static int fail_alloc, allocations;
static void *kmalloc(size_t n, int flags) { (void)flags; if (fail_alloc) return NULL; void *p=malloc(n); if(p) allocations++; return p; }
static void kfree(void *p) { if(p) allocations--; free(p); }
struct drm_panel { int n; };
/* TYPES */
static struct panel_event_notifier_entry *panel_event_notifier_entries[PANEL_EVENT_NOTIFIER_CLIENT_MAX];
static pthread_mutex_t panel_event_notifier_entries_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t panel_event_notifier_srcu = PTHREAD_RWLOCK_INITIALIZER;
typedef int (*mi_display_pwrkey_callback)(int);
static mi_display_pwrkey_callback mi_display_pwrkey_cb;
static pthread_mutex_t mi_display_pwrkey_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t mi_display_pwrkey_srcu = PTHREAD_RWLOCK_INITIALIZER;
/* FUNCTIONS */
static int hits;
static atomic_int entered, allow_exit, done;
static bool block;
static int data = 123;
static struct drm_panel panel;
static struct panel_event_notification notice = {.notif_type=DRM_PANEL_EVENT_UNBLANK, .panel=&panel};
static void *cookie;
static void callback(enum panel_event_notifier_tag tag, struct panel_event_notification *n, void *p) {
 assert(tag==PANEL_EVENT_NOTIFICATION_PRIMARY && n==&notice && p==&data);
 hits++;
 if(block) { atomic_store(&entered,1); while(!atomic_load(&allow_exit)) usleep(100); assert(*(int *)p==123); }
}
static int key_callback(int event) { assert(event==7); atomic_store(&entered,1); while(!atomic_load(&allow_exit)) usleep(100); return 0; }
static int replacement_hits;
static int key_replacement(int event) { assert(event==7); replacement_hits++; return 0; }
static void *reader(void *key) { if(key) mi_display_pwrkey_notify(7); else panel_event_notification_trigger(PANEL_EVENT_NOTIFICATION_PRIMARY,&notice); return NULL; }
static void *remover(void *key) { if(key) mi_display_pwrkey_callback_set(key_replacement); else panel_event_notifier_unregister(cookie); atomic_store(&done,1); return NULL; }
int main(void) {
 assert(panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_NONE,0,&panel,callback,&data)==ERR_PTR(-EINVAL));
 assert(panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_MAX,0,&panel,callback,&data)==ERR_PTR(-EINVAL));
 assert(panel_event_notifier_register(1,-1,&panel,callback,&data)==ERR_PTR(-EINVAL));
 assert(panel_event_notifier_register(1,PANEL_EVENT_NOTIFIER_CLIENT_MAX,&panel,callback,&data)==ERR_PTR(-EINVAL));
 assert(panel_event_notifier_register(1,0,&panel,NULL,&data)==ERR_PTR(-EINVAL));
 fail_alloc=1; assert(panel_event_notifier_register(1,0,&panel,callback,&data)==ERR_PTR(-ENOMEM)); fail_alloc=0;
 cookie=panel_event_notifier_register(1,0,&panel,callback,&data); assert(!IS_ERR_OR_NULL(cookie));
 assert(panel_event_notifier_register(1,0,&panel,callback,&data)==ERR_PTR(-EEXIST));
 for(int tag=-2;tag<=5;tag++) for(int type=-2;type<=10;type++) {
   notice.notif_type=type; int old=hits; panel_event_notification_trigger(tag,&notice);
   assert(hits-old==(tag==1 && type>DRM_PANEL_EVENT_NONE && type<DRM_PANEL_EVENT_MAX));
 }
 notice.notif_type=DRM_PANEL_EVENT_UNBLANK;
 panel_event_notifier_unregister(&data); panel_event_notifier_unregister(NULL); panel_event_notifier_unregister(ERR_PTR(-EINVAL));
 for(int k=0;k<2;k++) {
   pthread_t a,b; atomic_store(&entered,0); atomic_store(&allow_exit,0); atomic_store(&done,0); atomic_store(&draining,0);
   if(k) mi_display_pwrkey_callback_set(key_callback); else block=true;
   atomic_store(&draining,0);
   pthread_create(&a,NULL,reader,(void *)(intptr_t)k); while(!atomic_load(&entered)) usleep(100);
   pthread_create(&b,NULL,remover,(void *)(intptr_t)k); while(!atomic_load(&draining)) usleep(100);
   assert(!atomic_load(&done)); atomic_store(&allow_exit,1); pthread_join(a,NULL); pthread_join(b,NULL); assert(atomic_load(&done));
 }
 assert(!allocations); int old=hits; panel_event_notification_trigger(1,&notice); assert(hits==old);
 mi_display_pwrkey_notify(7); assert(replacement_hits==1); mi_display_pwrkey_callback_set(NULL); mi_display_pwrkey_notify(7); assert(replacement_hits==1);
 puts("{\"invalid_and_duplicate_registration\":true,\"notification_cases\":104,\"unregister_and_replacement_drain\":true}");
}
