#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef unsigned long (*cam_common_mini_dump_cb)(void *, unsigned long, void *);
#define CAM_COMMON_MINI_DUMP_DEV_NUM 6
#define CAM_COMMON_MINI_DUMP_DEV_NAME_LEN 16
#define CAM_COMMON_MINI_DUMP_SIZE (10 * 1024 * 1024)
#define CAM_WARN(...) ((void)0)
#define CAM_ERR(...) ((void)0)
#define GFP_KERNEL 0
#define NOTIFY_OK 1
#define NOTIFY_STOP_MASK 0x8000
#define __rcu
struct mutex { pthread_mutex_t value; };
#define DEFINE_MUTEX(n) struct mutex n = {PTHREAD_MUTEX_INITIALIZER}
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->value)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->value)); }
#define lockdep_is_held(p) ((void)(p), true)
static pthread_rwlock_t grace = PTHREAD_RWLOCK_INITIALIZER;
static atomic_int grace_waiters;
static void rcu_read_lock(void) { assert(!pthread_rwlock_rdlock(&grace)); }
static void rcu_read_unlock(void) { assert(!pthread_rwlock_unlock(&grace)); }
static void synchronize_rcu(void) {
    atomic_fetch_add(&grace_waiters, 1);
    assert(!pthread_rwlock_wrlock(&grace));
    assert(!pthread_rwlock_unlock(&grace));
    atomic_fetch_sub(&grace_waiters, 1);
}
#define rcu_dereference(p) __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define rcu_dereference_protected(p, c) ((void)(c), rcu_dereference(p))
#define rcu_assign_pointer(p, v) __atomic_store_n(&(p), (v), __ATOMIC_RELEASE)
#define RCU_INIT_POINTER(p, v) rcu_assign_pointer(p, v)
static int allocations, fail_alloc, fail_register, registrations, unregistrations;
static void *kzalloc(size_t n, int flags) {
    (void)flags;
    if (fail_alloc) return NULL;
    void *p = calloc(1, n); assert(p); allocations++; return p;
}
static void kfree(void *p) { if (p) { assert(allocations > 0); allocations--; free(p); } }
static int strscpy(char *to, const char *from, size_t n) {
    size_t len = strlen(from), used = len < n - 1 ? len : n - 1;
    memcpy(to, from, used); to[used] = 0; return len < n ? (int)len : -E2BIG;
}
struct cam_common_mini_dump_data {
    void *waddr[6]; uint8_t name[6][16]; unsigned long size[6];
};
struct notifier_block {
    int (*notifier_call)(struct notifier_block *, unsigned long, void *);
    int priority;
};
struct va_md_entry { unsigned long vaddr, size; char owner[32]; void (*cb)(void *, unsigned long); };
static struct notifier_block *registered;
static struct va_md_entry submitted;
static int qcom_va_md_register(const char *name, struct notifier_block *nb) {
    assert(!strcmp(name, "Camera") && !registered && nb->notifier_call);
    if (fail_register) return -ENODEV;
    registered = nb; registrations++; return 0;
}
static int qcom_va_md_unregister(const char *name, struct notifier_block *nb) {
    assert(!strcmp(name, "Camera") && registered == nb);
    registered = NULL; unregistrations++;
    synchronize_rcu(); return 0;
}
static int qcom_va_md_add_region(struct va_md_entry *entry) {
    assert(!entry->vaddr && entry->cb && !strcmp(entry->owner, "Camera"));
    assert(entry->size == CAM_COMMON_MINI_DUMP_SIZE);
    submitted = *entry; return 0;
}
#include "minidump_camera.inc"

struct client {
    unsigned long length, offered;
    atomic_int entered, resume, calls;
    bool alive, gated, overreport;
};
static unsigned long dump(void *dst, unsigned long len, void *priv) {
    struct client *c = priv;
    assert(c->alive); c->offered = len;
    atomic_fetch_add(&c->calls, 1); atomic_store(&c->entered, 1);
    while (c->gated && !atomic_load(&c->resume)) sched_yield();
    assert(c->alive);
    if (c->overreport) return len + 1;
    unsigned long n = c->length < len ? c->length : len;
    memset(dst, 0x6c, n); return n;
}
static atomic_int removal_done;
static void *capture_thread(void *p) {
    (void)p;
    unsigned char bytes[sizeof(struct cam_common_mini_dump_data) + 128];
    cam_common_mini_dump_handler(bytes, sizeof(bytes)); return NULL;
}
static void *remove_thread(void *p) {
    cam_common_unregister_mini_dump_cb(dump, p);
    ((struct client *)p)->alive = false;
    atomic_store(&removal_done, 1); return NULL;
}
static void add(struct client *c, const char *name) {
    assert(!cam_common_register_mini_dump_cb(dump, (uint8_t *)name, c));
}
static void remove_client(struct client *c) { cam_common_unregister_mini_dump_cb(dump, c); }
static void empty(void) {
    assert(!cam_mini_dump_count && !cam_mini_dump_registered && !registered && !allocations);
    for (int i = 0; i < 6; i++) assert(!cam_mini_dump_entries[i]);
    assert(registrations == unregistrations);
}
int main(void) {
    struct client c[7] = {0};
    for (int i = 0; i < 7; i++) { c[i].alive = true; c[i].length = i + 1; }
    assert(cam_common_register_mini_dump_cb(NULL, (uint8_t *)"x", c) == -EINVAL);
    assert(cam_common_register_mini_dump_cb(dump, NULL, c) == -EINVAL);
    fail_alloc = 1; assert(cam_common_register_mini_dump_cb(dump, (uint8_t *)"x", c) == -ENOMEM);
    fail_alloc = 0; fail_register = 1;
    assert(cam_common_register_mini_dump_cb(dump, (uint8_t *)"x", c) == -ENODEV);
    fail_register = 0; empty();
    for (int i = 0; i < 6; i++) add(&c[i], "literal%n%p");
    int prior_reg = registrations;
    add(&c[0], "duplicate"); assert(cam_mini_dump_count == 6 && registrations == prior_reg);
    assert(cam_common_register_mini_dump_cb(dump, (uint8_t *)"overflow", &c[6]) == -EINVAL);
    remove_client(&c[2]); add(&c[6], "replacement");
    assert(cam_mini_dump_count == 6 && registrations == prior_reg);
    assert(registered->notifier_call(registered, 0, NULL) == NOTIFY_OK);
    unsigned char bytes[sizeof(struct cam_common_mini_dump_data) + 128];
    submitted.cb(bytes, sizeof(bytes));
    struct cam_common_mini_dump_data *md = (void *)bytes;
    assert(!strcmp((char *)md->name[0], "literal%n%p"));
    assert(md->size[2] == c[6].length && !atomic_load(&c[2].calls));
    assert(md->waddr[0] == bytes + sizeof(*md));
    for (int i = 0; i < 7; i++) remove_client(&c[i]);
    empty();
    for (int i = 0; i < 100; i++) { add(c, "reuse"); remove_client(c); }
    empty();
    c[0].length = 16; c[1].length = 16; c[2].length = 1;
    add(&c[0], "012345678901234567890123456789"); add(&c[1], "second"); add(&c[2], "third");
    for (int i = 0; i < 3; i++) atomic_store(&c[i].calls, 0);
    memset(bytes, 0xa5, sizeof(bytes));
    cam_common_mini_dump_handler(NULL, sizeof(bytes));
    cam_common_mini_dump_handler(bytes, sizeof(*md) - 1);
    assert(bytes[0] == 0xa5);
    cam_common_mini_dump_handler(bytes, sizeof(*md));
    assert(!atomic_load(&c[0].calls));
    cam_common_mini_dump_handler(bytes, sizeof(*md) + 32);
    assert(c[0].offered == 32 && c[1].offered == 16 && !atomic_load(&c[2].calls));
    assert(md->name[0][15] == 0 && !strcmp((char *)md->name[0], "012345678901234"));
    assert(md->size[0] == 16 && md->size[1] == 16 && !md->size[2]);
    assert(bytes[sizeof(*md) + 32] == 0xa5);
    c[0].overreport = true;
    atomic_store(&c[1].calls, 0); cam_common_mini_dump_handler(bytes, sizeof(*md) + 3);
    assert(!md->size[0] && !md->waddr[0] && !atomic_load(&c[1].calls));
    c[0].overreport = false;
    for (int i = 0; i < 3; i++) remove_client(&c[i]);
    empty();
    for (int mode = 0; mode < 2; mode++) for (int i = 0; i < 50; i++) {
        struct client active = {.alive = true, .gated = true, .length = 3};
        struct client retained = {.alive = true, .length = 3};
        add(&active, "active"); if (mode) add(&retained, "retained");
        atomic_store(&removal_done, 0);
        pthread_t capture, teardown;
        assert(!pthread_create(&capture, NULL, capture_thread, NULL));
        while (!atomic_load(&active.entered)) sched_yield();
        assert(!pthread_create(&teardown, NULL, remove_thread, &active));
        while (!atomic_load(&removal_done) && !atomic_load(&grace_waiters)) sched_yield();
        assert(!atomic_load(&removal_done) && active.alive);
        atomic_store(&active.resume, 1);
        assert(!pthread_join(capture, NULL)); assert(!pthread_join(teardown, NULL));
        assert(!active.alive);
        remove_client(&retained); empty();
    }
    printf("{\"registration_failures\":4,\"registry_cases\":5,\"slot_reuse_cycles\":100,\"length_cases\":5,\"literal_and_bounded_names\":2,\"last_client_capture_races\":50,\"retained_client_capture_races\":50}\n");
}
