#!/usr/bin/env python3
"""Exercise extracted WLAN page-pool ownership and cleanup context.

Mocked QDF locking, timers and workers expose destructive calls under BH/spin
locks and shutdown ordering; this does not model real scheduler concurrency.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|QDF_STATUS|bool)\s+\n?' + name + r'\(', source, re.M)
    assert match, name
    start = match.start()
    brace = source.index('{', match.end())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DP_PAGE_POOL_MAX 4
#define DP_RX_PP_INACTIVE_TIMER_MS 100
#define QDF_TIMER_TYPE_WAKE_APPS 0
#define QDF_STATUS_SUCCESS 0
#define QDF_STATUS_E_FAILURE 1
#define QDF_IS_STATUS_SUCCESS(x) ((x) == 0)
#define dp_info(...) ((void)0)
typedef int QDF_STATUS;
typedef struct node { struct node *prev, *next; } qdf_list_node_t;
typedef struct { qdf_list_node_t head; } qdf_list_t;
typedef struct { int id, calls; bool full; } *qdf_page_pool_t;
typedef struct { bool held, destroyed; } qdf_spinlock_t;
typedef int qdf_atomic_t;
typedef struct { void (*fn)(void *); void *arg; bool pending, disabled; } qdf_work_t;
typedef struct { void (*fn)(void *); void *arg; bool pending, freed; } qdf_timer_t;
struct dp_rx_pp_params { qdf_list_node_t node; qdf_page_pool_t pp; size_t pool_size, pp_size; };
struct dp_rx_page_pool {
    struct dp_rx_pp_params main_pool[DP_PAGE_POOL_MAX], aux_pool;
    uint8_t active_pp_idx; qdf_spinlock_t pp_lock;
    size_t curr_pool_size, base_pool_size; qdf_atomic_t update_in_progress;
    qdf_timer_t pool_inactivity_timer; qdf_work_t pool_inactivity_work;
    qdf_list_t inactive_list; bool page_pool_init;
};
struct dp_soc { struct dp_rx_page_pool rx_pp[1]; void *wlan_cfg_ctx, *osdev; };
static int atomic_depth, destroyed, fail_alloc, upsize_status, cases;
static bool inject_timer;
static struct { int id, calls; bool full; } pools[24];
static void qdf_list_create(qdf_list_t *l, int unused) { (void)unused; l->head.next = l->head.prev = &l->head; }
static bool qdf_list_empty(qdf_list_t *l) { return l->head.next == &l->head; }
static void qdf_list_destroy(qdf_list_t *l) { assert(qdf_list_empty(l)); }
static void qdf_list_insert_back(qdf_list_t *l, qdf_list_node_t *n) {
    n->prev=l->head.prev; n->next=&l->head; n->prev->next=n; l->head.prev=n;
}
static void qdf_list_remove_node(qdf_list_t *l, qdf_list_node_t *n) {
    (void)l; n->prev->next=n->next; n->next->prev=n->prev;
}
#define entry(p, type, field) ((type *)((char *)(p) - offsetof(type, field)))
#define qdf_list_for_each_del(l, cur, nxt, field) \
    for (cur=entry((l)->head.next, __typeof__(*cur), field), \
         nxt=entry(cur->field.next, __typeof__(*cur), field); \
         &cur->field != &(l)->head; \
         cur=nxt, nxt=entry(nxt->field.next, __typeof__(*nxt), field))
static void qdf_spin_lock_bh(qdf_spinlock_t *l) { assert(!l->held && !l->destroyed); l->held=true; atomic_depth++; }
static void qdf_spin_unlock_bh(qdf_spinlock_t *l) { assert(l->held); l->held=false; atomic_depth--; }
#define qdf_spin_lock qdf_spin_lock_bh
#define qdf_spin_unlock qdf_spin_unlock_bh
static void qdf_spinlock_destroy(qdf_spinlock_t *l) { assert(!l->held); l->destroyed=true; }
static bool wlan_cfg_get_dp_rx_buffer_recycle(void *ctx) { (void)ctx; return true; }
static void qdf_page_pool_destroy(qdf_page_pool_t p) { assert(!atomic_depth); if(p) { assert(!p->calls++); destroyed++; } }
static bool qdf_page_pool_full_bh(qdf_page_pool_t p) { assert(atomic_depth); return p->full; }
static void *qdf_mem_malloc(size_t n) { return fail_alloc ? NULL : calloc(1,n); }
#define qdf_mem_free free
#define qdf_mem_copy memcpy
static void qdf_mem_set(void *p, size_t n, int v) { memset(p,v,n); }
static void qdf_atomic_init(int *a) { *a=0; }
static void qdf_atomic_set(int *a, int n) { *a=n; }
static void qdf_create_work(int h, qdf_work_t *w, void (*fn)(void *), void *a) {
    (void)h; *w=(qdf_work_t){.fn=fn,.arg=a};
}
static void qdf_sched_work(int h, qdf_work_t *w) { (void)h; assert(!w->disabled); w->pending=true; }
static void run_work(qdf_work_t *w) { if(w->pending) { w->pending=false; w->fn(w->arg); } }
static void qdf_disable_work(qdf_work_t *w) { assert(!atomic_depth); w->pending=false; w->disabled=true; }
static void qdf_timer_init(void *d, qdf_timer_t *t, void (*fn)(void *), void *a, int type) {
    (void)d; (void)type; *t=(qdf_timer_t){.fn=fn,.arg=a};
}
static void qdf_timer_mod(qdf_timer_t *t, int ms) { (void)ms; assert(!t->freed); t->pending=true; }
static void fire_timer(qdf_timer_t *t) { assert(!t->freed); t->pending=false; atomic_depth++; t->fn(t->arg); atomic_depth--; }
static void qdf_timer_free(qdf_timer_t *t) {
    assert(!atomic_depth);
    if(inject_timer) { fire_timer(t); inject_timer=false; }
    t->freed=true; t->pending=false;
}
static QDF_STATUS dp_rx_page_pool_upsize(struct dp_soc *s, struct dp_rx_page_pool *r, size_t n) {
    (void)s; (void)r; (void)n; return upsize_status;
}
'''

MAIN = r'''
static void setup(struct dp_soc *soc) {
    memset(soc,0,sizeof(*soc)); memset(pools,0,sizeof(pools));
    destroyed=fail_alloc=atomic_depth=upsize_status=0; inject_timer=false;
    for(int i=0;i<24;i++) { pools[i].id=i; pools[i].full=true; }
    struct dp_rx_page_pool *r=&soc->rx_pp[0];
    r->curr_pool_size=400; r->base_pool_size=100;
    for(int i=0;i<4;i++) r->main_pool[i].pp=(void *)&pools[i];
    r->aux_pool.pp=(void *)&pools[4];
    assert(dp_rx_page_pool_init(soc,0)==0);
}
static void inactive(struct dp_rx_page_pool *r, int id, bool full) {
    struct dp_rx_pp_params *p=calloc(1,sizeof(*p)); assert(p);
    pools[id].full=full; p->pp=(void *)&pools[id]; p->pool_size=100;
    qdf_list_insert_back(&r->inactive_list,&p->node);
}
static void clean_assert(struct dp_rx_page_pool *r) {
    assert(!atomic_depth && !r->page_pool_init);
    assert(r->pool_inactivity_timer.freed && !r->pool_inactivity_timer.pending);
    assert(r->pool_inactivity_work.disabled && !r->pool_inactivity_work.pending);
    assert(qdf_list_empty(&r->inactive_list));
    for(int i=0;i<4;i++) assert(!r->main_pool[i].pp);
    assert(!r->aux_pool.pp);
}
int main(void) {
    struct dp_soc soc; struct dp_rx_page_pool *r=&soc.rx_pp[0];
    for(int repeat=0;repeat<100;repeat++) {
        /* No pool destruction in timer/BH context; incomplete pools rearm. */
        setup(&soc); inactive(r,5,true); inactive(r,6,false);
        fire_timer(&r->pool_inactivity_timer); assert(!destroyed);
        run_work(&r->pool_inactivity_work); assert(destroyed==1);
        assert(r->pool_inactivity_timer.pending && !qdf_list_empty(&r->inactive_list));
        pools[6].full=true; fire_timer(&r->pool_inactivity_timer);
        run_work(&r->pool_inactivity_work); assert(destroyed==2);
        dp_rx_page_pool_free(&soc,0); assert(destroyed==7); clean_assert(r); cases++;
        /* Deinit wins against a timer that queues during synchronous stop. */
        setup(&soc); inactive(r,5,false); inject_timer=true;
        dp_rx_page_pool_free(&soc,0); assert(destroyed==6); clean_assert(r); cases++;
        /* Queued worker sees stopped state and cannot rearm the freed timer. */
        setup(&soc); inactive(r,5,false); r->page_pool_init=false;
        fire_timer(&r->pool_inactivity_timer); run_work(&r->pool_inactivity_work);
        assert(!destroyed && !r->pool_inactivity_timer.pending);
        r->page_pool_init=true; dp_rx_page_pool_free(&soc,0); clean_assert(r); cases++;
        /* Downsize detaches full pools and retains incomplete pools. */
        setup(&soc); pools[2].full=false;
        assert(dp_rx_page_pool_resize(&soc,0,100)==0); assert(destroyed==2);
        assert(r->main_pool[0].pp && !r->main_pool[1].pp && !r->main_pool[2].pp);
        assert(!qdf_list_empty(&r->inactive_list));
        assert(!r->update_in_progress && r->curr_pool_size==100);
        dp_rx_page_pool_free(&soc,0); assert(destroyed==5); clean_assert(r); cases++;
        /* Failed inactive-node allocation still destroys outside pp_lock. */
        setup(&soc); pools[1].full=pools[2].full=pools[3].full=false; fail_alloc=1;
        assert(dp_rx_page_pool_resize(&soc,0,100)==0); assert(destroyed==3);
        assert(qdf_list_empty(&r->inactive_list));
        dp_rx_page_pool_free(&soc,0); assert(destroyed==5); clean_assert(r); cases++;
        /* Partial allocation failure has no timer/work object to cancel. */
        setup(&soc); dp_rx_page_pool_deinit(&soc,0);
        r->main_pool[1].pp=NULL; r->aux_pool.pp=NULL;
        dp_rx_page_pool_free(&soc,0); assert(destroyed==3); cases++;
        /* Upsize failure preserves size and releases update flag. */
        setup(&soc); upsize_status=QDF_STATUS_E_FAILURE;
        assert(dp_rx_page_pool_resize(&soc,0,800)==QDF_STATUS_E_FAILURE);
        assert(r->curr_pool_size==400 && !r->update_in_progress);
        dp_rx_page_pool_free(&soc,0); clean_assert(r); cases++;
    }
    printf("PASS: %d extracted lifecycle/context cases\n",cases);
}
'''


names = ['dp_rx_page_pool_destroy_list', 'dp_rx_page_pool_inactive_work',
         'dp_rx_page_pool_inactive_timer', 'dp_rx_page_pool_deinit',
         'dp_rx_page_pool_init', 'dp_rx_page_pool_free', 'dp_rx_page_pool_resize']
source = (HERE / 'candidate.c').read_text()
actual = '\n\n'.join(function(source, name) for name in names)
original = (HERE / 'original.c').read_text()
variants = {
    'candidate': actual,
    'original-free': actual.replace(function(source, 'dp_rx_page_pool_free'), function(original, 'dp_rx_page_pool_free')),
    'timer-runs-cleanup': actual.replace('qdf_sched_work(0, &rx_pp->pool_inactivity_work);', 'dp_rx_page_pool_inactive_work(rx_pp);'),
    'missing-work-drain': actual.replace('qdf_disable_work(&rx_pp->pool_inactivity_work);', ''),
    'original-resize': actual.replace(function(source, 'dp_rx_page_pool_resize'), function(original, 'dp_rx_page_pool_resize')),
}
results = {}
with tempfile.TemporaryDirectory(prefix='onyx-page-pool-') as temporary:
    tmp = Path(temporary)
    for name, body in variants.items():
        c, exe = tmp / (name + '.c'), tmp / name
        c.write_text(PREFIX + body + MAIN)
        subprocess.run(['gcc', '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', str(c), '-o', str(exe)], check=True)
        p = subprocess.run([str(exe)], capture_output=True, text=True)
        results[name] = {'returncode': p.returncode, 'stdout': p.stdout,
                         'stderr': p.stderr[:1200]}
        assert (p.returncode == 0) == (name == 'candidate'), results[name]
        print(name, 'PASS' if p.returncode == 0 else 'regression rejected')
report = {'status': 'PASS', 'cases': 700, 'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
          'variants': results, 'limits': __doc__}
(HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
