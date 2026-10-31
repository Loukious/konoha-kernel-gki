#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define RECEIVE_OFFLOAD 1
#define HIF_NAPI_INITED 1
#define HIF_NAPI_CONF_UP 2
#define HIF_GET_SOFTC(x) ((struct hif_softc *)(x))
#define NAPI_PIPE2ID(x) ((x) + 1)
#define NAPI_ID2PIPE(x) ((x) - 1)
#define NAPI_DEBUG(...) ((void)0)
#define hif_err(...) ((void)0)
#define hif_warn(...) ((void)0)
#define hif_debug(...) ((void)0)
#define QDF_ASSERT(x) assert(x)
#define QDF_DEBUG_PANIC(...) abort()
#define QDF_STATUS_SUCCESS 0
#define QDF_STATUS_E_NOMEM 1
#define QCA_NAPI_BUDGET 64
#define DP_RX_THREADS_INVALID 0
#define RX_SHUTDOWN_EVENT 0
#define RX_POST_EVENT 1
#define dp_debug(...) ((void)0)
typedef int QDF_STATUS;
struct qdf_net_if;
struct net_device { int attached; };
typedef struct net_device qdf_dummy_netdev_t;
struct napi_struct { struct net_device *dev; bool enabled; int (*poll)(struct napi_struct *, int); };
typedef struct napi_struct qdf_napi_struct;
struct hif_opaque_softc;
struct CE_state { bool htt_rx_data, htt_tx_data; };
struct qca_napi_info {
    struct net_device *netdev, *rx_thread_netdev;
    struct napi_struct napi, rx_thread_napi;
    int scale, id, irq;
    void *hif_ctx, *lro_ctx;
};
struct qca_napi_data { unsigned state, flags, ce_map; int lock; struct qca_napi_info *napis[4]; };
struct qdf_device { void *dev; };
struct hif_softc { struct qca_napi_data napi_data; int ce_count; struct CE_state *ce_id_to_state[4]; struct qdf_device *qdf_dev; bool srng; };
struct hif_exec_context { bool inited; int numirq, os_irq[4]; void *sched_ops; uint32_t scale_bin_shift; };
struct hif_napi_exec_context { struct hif_exec_context exec_ctx; struct net_device *netdev; struct napi_struct napi; };
struct dp_rx_thread { qdf_dummy_netdev_t *netdev; struct napi_struct napi; void *task; unsigned event_flag; int wait_q, shutdown_event; };
struct dp_rx_tm_handle { int num_dp_rx_threads, state; struct dp_rx_thread **rx_thread; };
static int thread_wakes, thread_waits;
static void qdf_set_bit(int bit, unsigned *flags) { *flags |= 1U << bit; }
static void qdf_wake_up_interruptible(int *queue) { (*queue)++; thread_wakes++; }
static int qdf_wait_single_event(int *event, unsigned timeout) { assert(*event && !timeout); (*event)--; thread_waits++; return 0; }
static int alloc_calls, fail_at, objects, devices, attached, enabled, hooks, locks, cpu_error;
static int napi_sched_ops;
static bool fail_alloc(void) { return ++alloc_calls == fail_at; }
static void *qdf_mem_malloc(size_t n) { if (fail_alloc()) return NULL; objects++; return calloc(1, n); }
static void qdf_mem_free(void *p) { assert(p && objects); objects--; free(p); }
static struct net_device *alloc_netdev_dummy(int size) { assert(size == 0); if (fail_alloc()) return NULL; devices++; return calloc(1, sizeof(struct net_device)); }
static void free_netdev(struct net_device *dev) { assert(dev && devices && !dev->attached); devices--; free(dev); }
static void qdf_netif_napi_add(struct net_device *dev, struct napi_struct *napi, int (*poll)(struct napi_struct *, int), int weight) { assert(dev && !napi->dev && weight > 0); dev->attached++; attached++; napi->dev=dev; napi->poll=poll; }
static void qdf_netif_napi_del(struct napi_struct *napi) { assert(napi->dev && !napi->enabled); napi->dev->attached--; attached--; napi->dev=NULL; }
static void qdf_napi_enable(struct napi_struct *napi) { assert(napi->dev && !napi->enabled); napi->enabled=true; enabled++; }
static void qdf_napi_disable(struct napi_struct *napi) { assert(napi->dev && napi->enabled); napi->enabled=false; enabled--; }
static void qdf_spinlock_create(int *lock) { assert(!*lock); *lock=1; locks++; }
static void qdf_spinlock_destroy(int *lock) { assert(*lock); *lock=0; locks--; }
static int hif_napi_cpu_init(struct hif_opaque_softc *hif) { if (cpu_error) return cpu_error; hooks++; return 0; }
static int hif_napi_cpu_deinit(struct hif_opaque_softc *hif) { assert(hooks); hooks--; return 0; }
static bool ce_srng_based(struct hif_softc *hif) { return hif->srng; }
static int pld_get_irq(void *dev, int i) { return i + 10; }
static void *qdf_lro_init(void) { return NULL; }
static void qdf_lro_deinit(void *ctx) { assert(!ctx); }
static int hif_exec_poll(struct napi_struct *napi, int budget) { return 0; }
static struct hif_napi_exec_context *hif_exec_get_napi(struct hif_exec_context *ctx) { return (void *)ctx; }
static void hif_irq_affinity_remove(int irq) { }
static void hif_core_ctl_set_boost(bool enable) { }
