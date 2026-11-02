#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

typedef uint32_t u32;
typedef uint8_t u8;
#define IS_ENABLED(x) 1
#define EXTCON_DISP_DP 1
#define EXTCON_DISP_HDMI 2
#define EXPORT_SYMBOL_GPL(x)
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_debug(...) ((void)0)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(x, value) __atomic_store_n(&(x), (value), __ATOMIC_RELEASE)
struct task_struct { int unused; };
static _Thread_local struct task_struct current_task;
#define current (&current_task)
struct mutex { pthread_mutex_t p; struct task_struct *owner; };
struct device { int unused; };
struct platform_device { struct device dev; void *data; };
struct extcon_dev { int state; };
struct list_head { void *unused; };
static _Thread_local int thread_role;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static bool lookup_entered, lookup_release, withdraw_entered;
static bool ready_entered, ready_release, block_lookup, block_ready;
static atomic_bool withdraw_done;
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->p, NULL)); m->owner = NULL; }
static void mutex_lock(struct mutex *m)
{
    if (thread_role == 2) {
        pthread_mutex_lock(&gate_lock);
        withdraw_entered = true;
        pthread_cond_broadcast(&gate_cond);
        pthread_mutex_unlock(&gate_lock);
    }
    /* A same-task acquisition is a deterministic failure instead of a hang. */
    assert(READ_ONCE(m->owner) != current);
    assert(!pthread_mutex_lock(&m->p));
    WRITE_ONCE(m->owner, current);
}
static void mutex_unlock(struct mutex *m)
{
    assert(READ_ONCE(m->owner) == current);
    WRITE_ONCE(m->owner, NULL);
    assert(!pthread_mutex_unlock(&m->p));
}
#define lockdep_assert_held(m) assert(READ_ONCE((m)->owner) == current)
static void *platform_get_drvdata(struct platform_device *p) { return p->data; }
/* TYPES */

static struct msm_ext_disp ext;
static struct platform_device device;
static struct msm_ext_disp_init_data interface_data;
static struct extcon_dev cable;
static struct msm_ext_disp_audio_codec_ops *callback_ops;
static bool interface_present = true, ready_self_withdraw, extcon_self_withdraw;
static int ready_calls, extcon_calls, self_result;

static int fixture_ready(struct platform_device *pdev)
{
    assert(pdev == &device && READ_ONCE(ext.lock.owner) != current);
    ready_calls++;
    if (ready_self_withdraw) {
        self_result = msm_ext_disp_unregister_audio_codec(pdev, callback_ops);
        assert(!self_result);
        free(callback_ops);
        callback_ops = NULL;
    }
    if (block_ready) {
        pthread_mutex_lock(&gate_lock);
        ready_entered = true;
        pthread_cond_broadcast(&gate_cond);
        while (!ready_release)
            pthread_cond_wait(&gate_cond, &gate_lock);
        pthread_mutex_unlock(&gate_lock);
    }
    return 0;
}

static int msm_ext_disp_get_intf_data(struct msm_ext_disp *e,
        struct msm_ext_disp_codec_id *codec, struct msm_ext_disp_init_data **result)
{
    assert(e == &ext);
    lockdep_assert_held(&e->lock);
    if (block_lookup && thread_role == 1) {
        pthread_mutex_lock(&gate_lock);
        lookup_entered = true;
        pthread_cond_broadcast(&gate_cond);
        while (!lookup_release)
            pthread_cond_wait(&gate_cond, &gate_lock);
        pthread_mutex_unlock(&gate_lock);
    }
    *result = interface_present ? &interface_data : NULL;
    return interface_present ? 0 : -ENODEV;
}
static int extcon_get_state(struct extcon_dev *d, int type) { return d->state; }
static int extcon_set_state_sync(struct extcon_dev *d, int type, int state)
{
    lockdep_assert_held(&ext.lock);
    extcon_calls++;
    d->state = state;
    if (extcon_self_withdraw) {
        assert(ext.notify_owner == current);
        self_result = msm_ext_disp_unregister_audio_codec(&device, callback_ops);
        assert(!self_result);
        free(callback_ops);
        callback_ops = NULL;
    }
    return 0;
}
/* SOURCE */

static struct msm_ext_disp_codec_id codec = { .type = EXT_DISPLAY_TYPE_DP,
    .ctrl_id = 0, .stream_id = 0 };
static void *writer(void *data)
{
    thread_role = 1;
    assert(msm_ext_disp_select_audio_codec(&device, &codec) == 0);
    return NULL;
}
static void *withdrawer(void *ops)
{
    thread_role = 2;
    assert(msm_ext_disp_unregister_audio_codec(&device, ops) == 0);
    atomic_store(&withdraw_done, true);
    return NULL;
}
static void *publisher(void *ops)
{
    thread_role = 1;
    assert(msm_ext_disp_register_audio_codec(&device, ops) == 0);
    return NULL;
}

int main(void)
{
    pthread_t first, second;
    mutex_init(&ext.lock);
    device.data = &ext.ext_disp_data;
    ext.pdev = &device;
    ext.current_codec.type = EXT_DISPLAY_TYPE_MAX;
    ext.audio_sdev[0] = &cable;
    interface_data.codec_ops.ready = fixture_ready;
    struct msm_ext_disp_audio_codec_ops *ops = calloc(1, sizeof(*ops));
    struct msm_ext_disp_audio_codec_ops *other = calloc(1, sizeof(*other));
    assert(msm_ext_disp_register_audio_codec(NULL, ops) == -EINVAL);
    assert(msm_ext_disp_unregister_audio_codec(NULL, ops) == -EINVAL);
    struct platform_device missing = {0};
    assert(msm_ext_disp_unregister_audio_codec(&missing, ops) == -ENODEV);
    assert(msm_ext_disp_register_audio_codec(&device, ops) == 0);
    assert(msm_ext_disp_register_audio_codec(&device, ops) == -EINVAL);
    assert(msm_ext_disp_unregister_audio_codec(&device, other) == -ENOENT && ext.ops == ops);
    assert(msm_ext_disp_select_audio_codec(&device, &codec) == 0);
    assert(ops->ready == fixture_ready);
    assert(msm_ext_disp_audio_config(&device, &codec, EXT_DISPLAY_CABLE_DISCONNECT) == 0);
    assert(!ops->ready);
    assert(msm_ext_disp_select_audio_codec(&device, &codec) == 0);

    /* A publication operation holds the actual provider mutex while withdrawing waits. */
    block_lookup = true;
    atomic_store(&withdraw_done, false);
    assert(!pthread_create(&first, NULL, writer, NULL));
    pthread_mutex_lock(&gate_lock);
    while (!lookup_entered)
        pthread_cond_wait(&gate_cond, &gate_lock);
    pthread_mutex_unlock(&gate_lock);
    assert(!pthread_create(&second, NULL, withdrawer, ops));
    pthread_mutex_lock(&gate_lock);
    while (!withdraw_entered)
        pthread_cond_wait(&gate_cond, &gate_lock);
    assert(!atomic_load(&withdraw_done));
    lookup_release = true;
    pthread_cond_broadcast(&gate_cond);
    pthread_mutex_unlock(&gate_lock);
    assert(!pthread_join(first, NULL) && !pthread_join(second, NULL));
    assert(!ext.ops && atomic_load(&withdraw_done));
    block_lookup = false;
    free(ops);
    assert(msm_ext_disp_audio_config(&device, &codec, EXT_DISPLAY_CABLE_DISCONNECT) == 0);
    assert(msm_ext_disp_audio_notify(&device, &codec, EXT_DISPLAY_CABLE_CONNECT) == -EPERM);

    assert(msm_ext_disp_select_audio_codec(&device, &codec) == -EINVAL);
    assert(msm_ext_disp_register_audio_codec(&device, other) == 0);
    struct msm_ext_disp_audio_codec_ops stale = {0};
    assert(msm_ext_disp_unregister_audio_codec(&device, &stale) == -ENOENT && ext.ops == other);
    assert(msm_ext_disp_unregister_audio_codec(&device, other) == 0);
    assert(msm_ext_disp_unregister_audio_codec(&device, other) == 0);
    free(other);

    /* Ready can withdraw and free its table before register returns. */
    callback_ops = calloc(1, sizeof(*callback_ops));
    ready_self_withdraw = true;
    assert(msm_ext_disp_register_audio_codec(&device, callback_ops) == 0);
    assert(!ext.ops && !callback_ops && !self_result);
    ready_self_withdraw = false;

    /* The synchronous extcon callback owns the same publication lock. */
    callback_ops = calloc(1, sizeof(*callback_ops));
    ext.update_audio = true;
    cable.state = 0;
    extcon_self_withdraw = true;
    assert(msm_ext_disp_register_audio_codec(&device, callback_ops) == 0);
    assert(!ext.ops && !callback_ops && !self_result && extcon_calls > 0);
    extcon_self_withdraw = false;

    /* A detached ready snapshot may finish after table storage has gone. */
    ops = calloc(1, sizeof(*ops));
    block_ready = true;
    assert(!pthread_create(&first, NULL, publisher, ops));
    pthread_mutex_lock(&gate_lock);
    while (!ready_entered)
        pthread_cond_wait(&gate_cond, &gate_lock);
    pthread_mutex_unlock(&gate_lock);
    assert(msm_ext_disp_unregister_audio_codec(&device, ops) == 0);
    free(ops);
    pthread_mutex_lock(&gate_lock);
    ready_release = true;
    pthread_cond_broadcast(&gate_cond);
    pthread_mutex_unlock(&gate_lock);
    assert(!pthread_join(first, NULL));
    assert(!ext.ops);
    assert(!pthread_mutex_destroy(&ext.lock.p));
    puts("{\"identity_and_retry\":true,\"concurrent_table_access_drained\":true,"
         "\"ready_self_withdraw\":true,\"extcon_self_withdraw\":true,"
         "\"detached_ready_after_free\":true,\"all_published_table_routes\":true}");
    return 0;
}
