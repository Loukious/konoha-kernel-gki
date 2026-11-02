#!/usr/bin/env python3
"""Execute the source migration functions and enqueue prefix with frozen time.

This checks transfers of retained contributions, not scheduler locking, cycle
scaling, window rollover or the later runnable-statistics enqueue operations.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
source = (HERE / 'candidate-walt.c').read_text()
header = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/onyx/kernel/sched/walt/walt.h').read_text()


def function(text, name):
    pos = text.index(name + '(')
    start = text.rfind('\n', 0, pos) + 1
    brace = text.index('{', pos)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define TASK_WAKING 512
#define ENQUEUE_DELAYED 512
#define ENQUEUE_WAKEUP 1
#define DEFAULT_CGROUP_COLOC_ID 1
#define WALT_BUG_WALT 1
#define WALT_BUG_UPSTREAM 2
#define TASK_MIGRATE 3
#define TASK_UPDATE 4
#define READ_ONCE(x) (x)
#define unlikely(x) (x)
#define WALT_BUG(...) assert(!"unexpected WALT accounting invariant")
struct group_cpu_time {
    u64 curr_runnable_sum, prev_runnable_sum;
    u64 nt_curr_runnable_sum, nt_prev_runnable_sum;
};
struct walt_related_thread_group { int id; } group;
struct walt_task_struct {
    u64 window_start, cpu_cycles, curr_window, prev_window;
    int enqueue_after_migration, prev_cpu, new_cpu, prev_on_rq;
    struct walt_related_thread_group *grp;
    bool young;
};
struct task_struct {
    int cpu, on_rq, __state, pid;
    char comm[16];
    struct { bool sched_delayed; } se;
    struct walt_task_struct wts;
};
struct rq { int cpu; } rqs[8];
struct walt_rq {
    u64 window_start;
    struct group_cpu_time grp_time;
    bool notif_pending;
    struct task_struct *ed_task;
    int enqueue_counter;
} walt_rq[8];
int wakeup_ctr[8], migration_irqs;
bool walt_disabled;
struct walt_related_thread_group *related_thread_groups[2] = {0, &group};
#define per_cpu(array, cpu) array[cpu]
#define task_rq(p) (&rqs[(p)->cpu])
#define task_walt(p) (&(p)->wts)
#define cpu_rq(cpu) (&rqs[cpu])
#define cpu_of(rq) ((rq)->cpu)
#define task_cpu(p) ((p)->cpu)
#define task_thread_info(p) (p)
#define raw_smp_processor_id() 0
#define walt_lockdep_assert_rq(rq, p) ((void)0)
#define raw_spin_rq_lock(rq) ((void)0)
#define raw_spin_rq_unlock(rq) ((void)0)
#define walt_update_task_ravg(p, rq, event, time, irq) ((p)->wts.window_start = walt_rq[(rq)->cpu].window_start)
#define migrate_top_tasks_subtraction(p, rq) ((void)0)
#define migrate_top_tasks_addition(p, rq) ((void)0)
#define migrate_inter_cluster_subtraction(p, cpu, young) assert(!"non-group path not covered")
#define migrate_inter_cluster_addition(p, cpu, young) assert(!"non-group path not covered")
#define walt_get_cycle_counts_cb(cpu, time) 0
#define is_new_task(p) ((p)->wts.young)
#define same_freq_domain(a, b) ((a)/2 == (b)/2)
#define is_ed_enabled() false
#define is_ed_task(p, time) false
#define walt_sched_clock() 1
#define walt_rq_clock(rq) 1
#define is_per_cpu_kthread(p) false
#define walt_irq_work_queue(work) (++migration_irqs)
#define task_is_runnable(p) ((p)->on_rq && !(p)->se.sched_delayed)
'''

main = r'''
static int cases;
static void setup(struct task_struct *p, int src, bool delayed, bool young,
                  u64 curr, u64 prev) {
    memset(walt_rq, 0, sizeof(walt_rq));
    memset(wakeup_ctr, 0, sizeof(wakeup_ctr));
    memset(p, 0, sizeof(*p));
    migration_irqs = 0;
    for (int i = 0; i < 8; i++) { rqs[i].cpu = i; walt_rq[i].window_start = 1; }
    p->cpu = src; p->on_rq = 2; p->se.sched_delayed = delayed;
    p->wts.grp = &group; p->wts.young = young;
    p->wts.curr_window = curr; p->wts.prev_window = prev;
    p->wts.prev_cpu = src; p->wts.new_cpu = -1;
    for (int i = 0; i < 8; i++) {
        struct group_cpu_time *g = &walt_rq[i].grp_time;
        g->curr_runnable_sum = 77; g->prev_runnable_sum = 83;
        g->nt_curr_runnable_sum = 17; g->nt_prev_runnable_sum = 23;
    }
    struct group_cpu_time *g = &walt_rq[src].grp_time;
    g->curr_runnable_sum += curr; g->prev_runnable_sum += prev;
    if (young) { g->nt_curr_runnable_sum += curr; g->nt_prev_runnable_sum += prev; }
}
static void verify(struct task_struct *p, int dst) {
    assert(p->wts.enqueue_after_migration == 0);
    assert(p->wts.new_cpu == -1);
    for (int i = 0; i < 8; i++) {
        struct group_cpu_time *g = &walt_rq[i].grp_time;
        bool target = i == dst;
        assert(g->curr_runnable_sum == 77 + (target ? p->wts.curr_window : 0));
        assert(g->prev_runnable_sum == 83 + (target ? p->wts.prev_window : 0));
        assert(g->nt_curr_runnable_sum == 17 + (target && p->wts.young ? p->wts.curr_window : 0));
        assert(g->nt_prev_runnable_sum == 23 + (target && p->wts.young ? p->wts.prev_window : 0));
    }
    cases++;
}
static void migrate(struct task_struct *p, int dst, int flags) {
    migrate_busy_time_subtraction(p, dst);
    p->cpu = dst;
    android_rvh_enqueue_task(NULL, &rqs[dst], p, flags);
    verify(p, dst);
}
int main(void) {
    u64 values[] = {0, 1, 27988, 196710, 16000000, UINT32_MAX};
    for (int src = 0; src < 8; src++) for (int dst = 0; dst < 8; dst++) {
        if (src == dst) continue;
        for (int young = 0; young < 2; young++) {
            for (unsigned c = 0; c < sizeof(values)/sizeof(values[0]); c++)
            for (unsigned v = 0; v < sizeof(values)/sizeof(values[0]); v++) {
                struct task_struct p;
                setup(&p, src, true, young, values[c], values[v]);
                migrate(&p, dst, 0);
                assert(walt_rq[dst].enqueue_counter == 0);
                assert(migration_irqs == (src/2 != dst/2));
                // A second migration must start with contributions on dst.
                migrate(&p, src, 0);
                assert(walt_rq[src].enqueue_counter == 0);
                // A delayed wakeup must not repeat the completed transfer.
                android_rvh_enqueue_task(NULL, &rqs[src], &p, ENQUEUE_DELAYED | ENQUEUE_WAKEUP);
                verify(&p, src);
                assert(walt_rq[src].enqueue_counter == 1);
                setup(&p, src, false, young, values[c], values[v]);
                migrate(&p, dst, 0);
                assert(walt_rq[dst].enqueue_counter == 1);
                setup(&p, src, false, young, values[c], values[v]);
                p.on_rq = 0; p.__state = TASK_WAKING;
                migrate(&p, dst, ENQUEUE_WAKEUP);
                // A sleeping, unqueued task must not start a new transfer.
                p.on_rq = 0; p.__state = 1;
                migrate_busy_time_subtraction(&p, src);
                verify(&p, dst);
            }
        }
    }
    printf("PASS %d retained-runtime lifecycle cases\n", cases);
}
'''


def harness(text):
    enqueue = function(text, 'static void android_rvh_enqueue_task')
    # Keep every operation before runnable-statistics update; count the gate.
    enqueue = enqueue[:enqueue.index('\twts->prev_on_rq = 1;')] + '\n}\n'
    return (prefix + function(header, 'static inline bool walt_task_enqueue_runnable')
            + function(text, 'static void migrate_busy_time_subtraction')
            + function(text, 'static void migrate_busy_time_addition') + enqueue + main)


results = {}
variants = {'candidate': source, 'original': (HERE / 'before-walt.c').read_text()}
variants['old-source-gate'] = source.replace('if (!p->on_rq && READ_ONCE(p->__state)',
                                            'if (!task_is_runnable(p) && READ_ONCE(p->__state)', 1)
gate = '\tif (!walt_task_enqueue_runnable(p, flags))\n\t\treturn;'
start = source.index('static void android_rvh_enqueue_task(')
hook = source[start:]
assert hook.count(gate) == 1
hook = hook.replace(gate + '\n', '', 1)
anchor = '\twallclock = walt_rq_clock(rq);'
hook = hook.replace(anchor, gate + '\n\n' + anchor, 1)
variants['old-destination-gate'] = source[:start] + hook
corrupt_fields = ['curr_runnable_sum', 'prev_runnable_sum',
                  'nt_curr_runnable_sum', 'nt_prev_runnable_sum']
for field in corrupt_fields:
    variants['corrupt-' + field] = source
with tempfile.TemporaryDirectory(prefix='walt-delayed-migration-') as temp:
    temp = Path(temp)
    for name, text in variants.items():
        c = temp / (name + '.c')
        content = harness(text)
        if name.startswith('corrupt-'):
            field = name.removeprefix('corrupt-')
            contribution = 27988 if 'prev' in field else 196710
            content = content[:content.index('int main(void) {')] + f'''
int main(void) {{
    struct task_struct p;
    setup(&p, 0, true, true, 196710, 27988);
    walt_rq[0].grp_time.{field} = {contribution - 1};
    migrate_busy_time_subtraction(&p, 2);
    assert(!"underflow was not detected before subtraction");
}}
'''
        c.write_text(content)
        exe = temp / name
        subprocess.run(['/home/loukious/Android/konoha-android17-6.18-port-20261031/prebuilts/clang/host/linux-x86/clang-r596125/bin/clang', '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', str(c), '-o', str(exe)], check=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        results[name] = {'returncode': run.returncode, 'stdout': run.stdout.strip(),
                         'stderr': run.stderr.strip(),
                         'extracted_harness_sha256': hashlib.sha256(c.read_bytes()).hexdigest()}
        assert (run.returncode == 0) == (name == 'candidate'), results[name]
        if name.startswith('corrupt-'):
            assert 'unexpected WALT accounting invariant' in run.stderr, results[name]
report = {'status': 'PASS', 'cases': results['candidate']['stdout'], 'variants': results,
          'limits': __doc__.strip(),
          'candidate_sha256': hashlib.sha256(source.encode()).hexdigest()}
(HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(results['candidate']['stdout'])
print('Original and both independent gate regressions rejected')
