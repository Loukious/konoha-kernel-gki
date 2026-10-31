#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
#define GFP_KERNEL 0
#define LPASS_CDC_WSA_MACRO_SWR_STRING_LEN 80
#define LPASS_CDC_WSA_MACRO_CHILD_DEVICES_MAX 3
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
struct mutex { pthread_mutex_t native; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->native, NULL)); }
static void mutex_destroy(struct mutex *m) { assert(!pthread_mutex_destroy(&m->native)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->native)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->native)); }
typedef struct { pthread_mutex_t lock; pthread_cond_t changed; } wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t *q) {
    assert(!pthread_mutex_init(&q->lock, NULL));
    assert(!pthread_cond_init(&q->changed, NULL));
}
static void wake_up_all(wait_queue_head_t *q) {
    assert(!pthread_mutex_lock(&q->lock));
    assert(!pthread_cond_broadcast(&q->changed));
    assert(!pthread_mutex_unlock(&q->lock));
}
#define wait_event(q, condition) do { \
    assert(!pthread_mutex_lock(&(q).lock)); \
    while (!(condition)) assert(!pthread_cond_wait(&(q).changed, &(q).lock)); \
    assert(!pthread_mutex_unlock(&(q).lock)); \
} while (0)

struct device_node {
    const char *name;
    int refs;
    unsigned int count;
    struct device_node *children[4];
};
struct device { struct device *parent; struct device_node *of_node; };
struct platform_device { struct device dev; bool registered, data; unsigned int id; char name[80]; };
struct lpass_cdc_wsa_macro_swr_ctrl_data { struct platform_device *wsa_swr_pdev; };
struct lpass_cdc_wsa_macro_swr_ctrl_platform_data { unsigned int cookie; };
struct lpass_cdc_wsa_macro_priv {
    struct device *dev;
    struct mutex swr_ctrl_lock;
    wait_queue_head_t swr_ctrl_wait;
    unsigned int swr_ctrl_users;
    struct lpass_cdc_wsa_macro_swr_ctrl_data *swr_ctrl_data;
    struct lpass_cdc_wsa_macro_swr_ctrl_platform_data swr_plat_data;
    struct platform_device *pdev_child_devices[3];
    unsigned int child_count;
};

static int allocation_count, failure_at, step;
static unsigned int serial, last_removed, notifications;
static bool block_callback, callback_entered, callback_continue, reenter;
static bool remove_done;
static struct lpass_cdc_wsa_macro_priv *active_rx;
static pthread_mutex_t callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_changed = PTHREAD_COND_INITIALIZER;
static bool fail(void) { return ++step == failure_at; }
static void *kcalloc(size_t n, size_t z, int flags) {
    (void)flags;
    if (fail()) return NULL;
    void *p = calloc(n, z);
    assert(p); allocation_count++;
    return p;
}
static void kfree(void *p) {
    if (p) { assert(allocation_count > 0); allocation_count--; free(p); }
}
static struct device_node *of_node_get(struct device_node *n) {
    if (n) { assert(n->refs > 0); n->refs++; }
    return n;
}
static void of_node_put(struct device_node *n) { if (n) { assert(n->refs > 1); n->refs--; } }
static unsigned int of_get_available_child_count(struct device_node *n) { return n->count; }
static struct device_node *next_child(struct device_node *root, struct device_node *previous) {
    unsigned int index = 0;
    if (previous) {
        while (index < root->count && root->children[index] != previous) index++;
        assert(index < root->count);
        of_node_put(previous);
        index++;
    }
    return index < root->count ? of_node_get(root->children[index]) : NULL;
}
#define for_each_available_child_of_node(root, node) \
    for (node = next_child(root, NULL); node; node = next_child(root, node))
static char *strnstr(const char *haystack, const char *needle, size_t count) {
    size_t len = strlen(needle);
    for (size_t i = 0; i + len <= count && haystack[i]; i++)
        if (!strncmp(haystack + i, needle, len)) return (char *)haystack + i;
    return NULL;
}
static int strscpy(char *dst, const char *src, size_t count) {
    size_t n = strlen(src);
    assert(count);
    if (n >= count) n = count - 1;
    memcpy(dst, src, n); dst[n] = 0;
    return (int)n;
}
static struct platform_device *platform_device_alloc(const char *name, int id) {
    assert(id == -1);
    struct platform_device *p = kcalloc(1, sizeof(*p), 0);
    if (p) { p->id = serial++; strscpy(p->name, name, sizeof(p->name)); }
    return p;
}
static int platform_device_add_data(struct platform_device *p, const void *data, size_t len) {
    assert(!p->registered && data && len == sizeof(struct lpass_cdc_wsa_macro_swr_ctrl_platform_data));
    if (fail()) return -ENOMEM;
    p->data = true;
    return 0;
}
static int platform_device_add(struct platform_device *p) {
    assert(!p->registered && p->dev.parent == active_rx->dev);
    assert(p->dev.of_node->refs >= 3);
    if (!strcmp(p->name, "wsa_swr_ctrl")) assert(p->data);
    if (fail()) return -EIO;
    p->registered = true;
    return 0;
}
static void platform_device_put(struct platform_device *p) {
    assert(!p->registered);
    of_node_put(p->dev.of_node);
    kfree(p);
}
static void platform_device_unregister(struct platform_device *p) {
    assert(p && p->registered && p->id < last_removed);
    assert(!active_rx->swr_ctrl_data && !active_rx->swr_ctrl_users);
    last_removed = p->id;
    p->registered = false;
    platform_device_put(p);
}

static int lpass_cdc_wsa_macro_notify_swr(struct lpass_cdc_wsa_macro_priv *, u32, void *);
static int swrm_wcd_notify(struct platform_device *p, u32 event, void *data) {
    (void)data;
    assert(p && p->registered);
    pthread_mutex_lock(&callback_lock);
    notifications++;
    if (event == 1 && block_callback) {
        callback_entered = true;
        pthread_cond_broadcast(&callback_changed);
        while (!callback_continue) pthread_cond_wait(&callback_changed, &callback_lock);
    }
    pthread_mutex_unlock(&callback_lock);
    if (event == 1 && reenter) {
        int rc = lpass_cdc_wsa_macro_notify_swr(active_rx, 2, NULL);
        assert(rc == 0 || rc == -EIO);
    }
    assert(p->registered);
    return -EIO;
}

#include "children.inc"

static void init(struct lpass_cdc_wsa_macro_priv *rx, struct device *dev) {
    assert(!allocation_count);
    memset(rx, 0, sizeof(*rx));
    rx->dev = dev;
    mutex_init(&rx->swr_ctrl_lock);
    init_waitqueue_head(&rx->swr_ctrl_wait);
    active_rx = rx;
    step = failure_at = 0;
    serial = notifications = 0;
    last_removed = UINT_MAX;
    block_callback = callback_entered = callback_continue = reenter = remove_done = false;
}
static void finish(struct lpass_cdc_wsa_macro_priv *rx) {
    assert(!allocation_count && !rx->child_count && !rx->swr_ctrl_users && !rx->swr_ctrl_data);
    for (int i = 0; i < 3; i++) assert(!rx->pdev_child_devices[i]);
    mutex_destroy(&rx->swr_ctrl_lock);
    assert(!pthread_mutex_destroy(&rx->swr_ctrl_wait.lock));
    assert(!pthread_cond_destroy(&rx->swr_ctrl_wait.changed));
}
static void *notify_thread(void *arg) {
    assert(lpass_cdc_wsa_macro_notify_swr(arg, 1, NULL) == -EIO);
    return NULL;
}
static void *remove_thread(void *arg) {
    lpass_cdc_wsa_macro_remove_child_devices(arg);
    pthread_mutex_lock(&callback_lock);
    remove_done = true;
    pthread_cond_broadcast(&callback_changed);
    pthread_mutex_unlock(&callback_lock);
    return NULL;
}

int main(void) {
    struct device_node nodes[4] = {
        {.name = "wsa_swr_master", .refs = 1}, {.name = "msm_cdc_pinctrl_a", .refs = 1},
        {.name = "msm_cdc_pinctrl_b", .refs = 1}, {.name = "msm_cdc_pinctrl_overflow", .refs = 1},
    };
    struct device_node root = {.name = "rx", .refs = 1, .count = 3,
                               .children = {&nodes[0], &nodes[1], &nodes[2], &nodes[3]}};
    struct device dev = {.of_node = &root};
    struct lpass_cdc_wsa_macro_priv rx;
    init(&rx, &dev);
    assert(!lpass_cdc_wsa_macro_add_child_devices(&rx));
    int sites = step;
    assert(rx.child_count == 3 && rx.swr_ctrl_data && rx.swr_ctrl_data[0].wsa_swr_pdev->data);
    reenter = true;
    assert(lpass_cdc_wsa_macro_notify_swr(&rx, 1, NULL) == -EIO && notifications == 2);
    rx.swr_ctrl_users = UINT_MAX;
    assert(lpass_cdc_wsa_macro_notify_swr(&rx, 1, NULL) == -EOVERFLOW && notifications == 2);
    rx.swr_ctrl_users = 0;
    lpass_cdc_wsa_macro_remove_child_devices(&rx);
    assert(!lpass_cdc_wsa_macro_notify_swr(&rx, 1, NULL));
    finish(&rx);
    for (int site = 1; site <= sites; site++) {
        init(&rx, &dev); failure_at = site;
        assert(lpass_cdc_wsa_macro_add_child_devices(&rx) < 0);
        finish(&rx);
        for (int i = 0; i < 4; i++) assert(nodes[i].refs == 1);
    }
    for (unsigned int count = 0; count <= 4; count++) {
        init(&rx, &dev); root.count = count;
        int rc = lpass_cdc_wsa_macro_add_child_devices(&rx);
        assert(count <= 3 ? !rc : rc == -EOVERFLOW);
        lpass_cdc_wsa_macro_remove_child_devices(&rx);
        finish(&rx);
    }
    root.count = 3;
    nodes[0].name = "non-swr";
    init(&rx, &dev);
    assert(!lpass_cdc_wsa_macro_add_child_devices(&rx) && rx.child_count == 2 && !rx.swr_ctrl_data);
    lpass_cdc_wsa_macro_remove_child_devices(&rx);
    finish(&rx);
    nodes[0].name = "ignored-a";
    nodes[1].name = "ignored-b";
    nodes[2].name = "ignored-c";
    init(&rx, &dev);
    assert(!lpass_cdc_wsa_macro_add_child_devices(&rx) && !rx.child_count && !step);
    finish(&rx);
    nodes[1].name = "msm_cdc_pinctrl_a";
    nodes[2].name = "msm_cdc_pinctrl_b";
    nodes[0].name = "wsa_swr_master";
    for (int race = 0; race < 100; race++) {
        init(&rx, &dev);
        assert(!lpass_cdc_wsa_macro_add_child_devices(&rx));
        block_callback = true; reenter = true;
        pthread_t notifier, remover;
        assert(!pthread_create(&notifier, NULL, notify_thread, &rx));
        pthread_mutex_lock(&callback_lock);
        while (!callback_entered) pthread_cond_wait(&callback_changed, &callback_lock);
        pthread_mutex_unlock(&callback_lock);
        assert(!pthread_create(&remover, NULL, remove_thread, &rx));
        for (;;) {
            mutex_lock(&rx.swr_ctrl_lock);
            bool withdrawn = rx.swr_ctrl_data == NULL;
            mutex_unlock(&rx.swr_ctrl_lock);
            if (withdrawn) break;
            sched_yield();
        }
        pthread_mutex_lock(&callback_lock);
        assert(!remove_done);
        callback_continue = true;
        pthread_cond_broadcast(&callback_changed);
        pthread_mutex_unlock(&callback_lock);
        assert(!pthread_join(notifier, NULL) && !pthread_join(remover, NULL));
        assert(notifications == 1 && remove_done);
        finish(&rx);
    }
    for (int i = 0; i < 4; i++) assert(nodes[i].refs == 1);
    printf("{\"setup_failure_sites\":%d,\"child_count_cases\":5,\"non_swr_children\":1,"
           "\"notification_reentry\":1,\"notification_overflow\":1,\"withdrawal_reentry_races\":100}\n", sites);
}
