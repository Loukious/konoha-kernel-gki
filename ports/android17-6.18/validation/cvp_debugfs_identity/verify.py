#!/usr/bin/env python3
"""Exercise production EVA debugfs identity allocation and removal paths."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
LIVE = W / 'onyx/qcom/opensource/eva-kernel/msm/eva/msm_cvp_debug.c'
candidate = (H / 'candidate.c').read_text()
before = (H / 'before.c').read_text()


def function(source, name):
    match = re.search(r'^(?:struct dentry \*|void )' + name + r'\([^;]+?\n\{', source, re.M)
    assert match, name
    start = match.start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


adapter = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_DEBUGFS_NAME 50
#define GFP_KERNEL 0
#define FRAME_PROCESSING 0
#define CVP_ERR 0
#define dprintk(...) ((void)0)
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
struct inode { void *i_private; };
struct dentry {
    struct inode storage, *d_inode;
    struct dentry *parent, *child;
    char name[MAX_DEBUGFS_NAME];
};
struct msm_cvp_core { unsigned index; };
struct msm_cvp_inst {
    struct msm_cvp_core *core;
    struct dentry *debugfs_root;
    struct { struct { bool sampling; } pdata[1]; } debug;
};
struct file_operations { unsigned dummy; };
static const struct file_operations inst_info_fops;
struct ida { unsigned dummy; };
#define DEFINE_IDA(name) struct ida name
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static int used_ids[2048];
static unsigned id_count, tree_count;
static struct dentry *trees[2048];
static atomic_int allocations;
static _Thread_local unsigned failure;
static _Thread_local int force_id = -1;
static void *kzalloc(size_t size, int flags) {
    assert(flags == GFP_KERNEL);
    if (failure == 1) return NULL;
    void *p = calloc(1, size); assert(p);
    atomic_fetch_add(&allocations, 1); return p;
}
static void kfree(void *p) {
    assert(p); free(p); assert(atomic_fetch_sub(&allocations, 1) > 0);
}
static int ida_alloc(struct ida *ids, int flags) {
    assert(ids && flags == GFP_KERNEL);
    if (failure == 2) return -ENOMEM;
    if (failure == 3) return -ENOSPC;
    assert(!pthread_mutex_lock(&state_lock));
    int id = force_id >= 0 ? force_id : 0;
    force_id = -1;
    bool taken;
    do {
        taken = false;
        for (unsigned i = 0; i < id_count; i++)
            if (used_ids[i] == id) { taken = true; id++; break; }
    } while (taken);
    assert(id_count < 2048); used_ids[id_count++] = id;
    assert(!pthread_mutex_unlock(&state_lock));
    sched_yield(); return id;
}
static void ida_free(struct ida *ids, unsigned id) {
    assert(ids); assert(!pthread_mutex_lock(&state_lock));
    char name[MAX_DEBUGFS_NAME];
    snprintf(name, sizeof(name), "inst_%u", id);
    for (unsigned i = 0; i < tree_count; i++)
        assert(strcmp(name, trees[i]->name));
    unsigned i;
    for (i = 0; i < id_count; i++) if (used_ids[i] == (int)id) break;
    assert(i < id_count); used_ids[i] = used_ids[--id_count];
    assert(!pthread_mutex_unlock(&state_lock));
}
static int kernel_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int n;
    if (!strcmp(fmt, "inst_%pK"))
        n = snprintf(buf, size, "inst_0000000000000000");
    else n = vsnprintf(buf, size, fmt, args);
    va_end(args); return n;
}
#define snprintf kernel_snprintf
static struct dentry *debugfs_create_dir(const char *name, struct dentry *parent) {
    assert(parent && !IS_ERR_OR_NULL(parent));
    if (failure == 4) return NULL;
    if (failure == 5) return ERR_PTR(-ENOMEM);
    if (failure == 6) return ERR_PTR(-ENODEV);
    assert(strlen(name) < MAX_DEBUGFS_NAME);
    assert(!pthread_mutex_lock(&state_lock));
    for (unsigned i = 0; i < tree_count; i++)
        if (trees[i]->parent == parent && !strcmp(trees[i]->name, name)) {
            assert(!pthread_mutex_unlock(&state_lock)); return ERR_PTR(-EEXIST);
        }
    struct dentry *d = calloc(1, sizeof(*d)); assert(d);
    d->parent = parent; d->d_inode = &d->storage;
    strcpy(d->name, name); assert(tree_count < 2048); trees[tree_count++] = d;
    assert(!pthread_mutex_unlock(&state_lock)); sched_yield(); return d;
}
static struct dentry *debugfs_create_file(const char *name, unsigned mode,
        struct dentry *parent, void *data, const struct file_operations *fops) {
    assert(!strcmp(name, "info") && mode == 0444 && fops == &inst_info_fops);
    assert(data && parent && !parent->child);
    if (failure == 7) return NULL;
    if (failure == 8) return ERR_PTR(-ENOMEM);
    if (failure == 9) return ERR_PTR(-ENODEV);
    struct dentry *d = calloc(1, sizeof(*d)); assert(d);
    d->d_inode = &d->storage; d->d_inode->i_private = data;
    parent->child = d; return d;
}
static void debugfs_remove_recursive(struct dentry *d) {
    assert(d && !IS_ERR_OR_NULL(d));
    assert(!pthread_mutex_lock(&state_lock));
    unsigned i;
    for (i = 0; i < tree_count; i++) if (trees[i] == d) break;
    assert(i < tree_count); trees[i] = trees[--tree_count];
    free(d->child); free(d);
    assert(!pthread_mutex_unlock(&state_lock)); sched_yield();
}
static void check_empty(void) {
    assert(!id_count && !tree_count && !atomic_load(&allocations));
}
'''

main = r'''
static void remove_inst(struct msm_cvp_inst *inst) {
    msm_cvp_debugfs_deinit_inst(inst);
    assert(!inst->debugfs_root);
    msm_cvp_debugfs_deinit_inst(inst);
}
struct worker { struct msm_cvp_core *core; struct dentry *parent; };
static pthread_barrier_t barrier;
static void *worker(void *arg) {
    struct worker *w = arg;
    struct msm_cvp_inst inst[64] = {0};
    for (unsigned i = 0; i < 64; i++) {
        inst[i].core = w->core;
        inst[i].debugfs_root = msm_cvp_debugfs_init_inst(&inst[i], w->parent);
        assert(inst[i].debugfs_root);
    }
    int rc = pthread_barrier_wait(&barrier);
    assert(!rc || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < 64; i++) remove_inst(&inst[i]);
    return NULL;
}
int main(void) {
    unsigned cases = 0;
    struct dentry parent[2] = {0};
    struct msm_cvp_core core[2] = {{.index=0}, {.index=1}};
    struct msm_cvp_inst inst = {.core=&core[0]};
    assert(!msm_cvp_debugfs_init_inst(NULL, &parent[0])); cases++;
    assert(!msm_cvp_debugfs_init_inst(&inst, NULL)); cases++;
    assert(!msm_cvp_debugfs_init_inst(&inst, ERR_PTR(-ENODEV))); cases++;
    msm_cvp_debugfs_deinit_inst(NULL);
    for (unsigned repeat = 0; repeat < 8; repeat++)
    for (failure = 1; failure <= 9; failure++) {
        assert(!msm_cvp_debugfs_init_inst(&inst, &parent[0]));
        check_empty(); cases++;
    }
    failure = 0;
    struct msm_cvp_inst simultaneous[128] = {0};
    for (unsigned i = 0; i < 128; i++) {
        simultaneous[i].core = &core[i % 2];
        simultaneous[i].debugfs_root = msm_cvp_debugfs_init_inst(
            &simultaneous[i], &parent[i % 2]);
        assert(simultaneous[i].debugfs_root);
        assert(simultaneous[i].debug.pdata[FRAME_PROCESSING].sampling);
        struct cvp_core_inst_pair *idata = simultaneous[i].debugfs_root->d_inode->i_private;
        assert(idata->inst == &simultaneous[i] && idata->core == simultaneous[i].core);
        cases++;
    }
    for (unsigned i = 0; i < 128; i++) remove_inst(&simultaneous[i]);
    check_empty();
    for (unsigned i = 0; i < 512; i++) {
        inst.debugfs_root = msm_cvp_debugfs_init_inst(&inst, &parent[0]);
        assert(inst.debugfs_root && !strcmp(inst.debugfs_root->name, "inst_0"));
        remove_inst(&inst); check_empty(); cases++;
    }
    force_id = INT_MAX;
    inst.debugfs_root = msm_cvp_debugfs_init_inst(&inst, &parent[0]);
    assert(inst.debugfs_root && !strcmp(inst.debugfs_root->name, "inst_2147483647"));
    remove_inst(&inst); check_empty(); cases++;
    inst.debugfs_root = ERR_PTR(-ENODEV);
    msm_cvp_debugfs_deinit_inst(&inst); inst.debugfs_root = NULL; check_empty(); cases++;
    unsigned concurrent_instances = 0;
    for (unsigned round = 0; round < 8; round++) {
        pthread_t threads[8]; struct worker workers[8];
        assert(!pthread_barrier_init(&barrier, NULL, 8));
        for (unsigned i = 0; i < 8; i++) {
            workers[i] = (struct worker){.core=&core[i%2], .parent=&parent[i%2]};
            assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
        }
        for (unsigned i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
        assert(!pthread_barrier_destroy(&barrier)); check_empty();
        concurrent_instances += 512;
    }
    printf("PASS: %u lifecycle cases, %u concurrent instance trees\n", cases, concurrent_instances);
    return 0;
}
'''

variants = {'candidate': candidate, 'original': before}
variants['pointer-name'] = candidate.replace('"inst_%d",\n\t\t idata->debugfs_id', '"inst_%pK", inst')
variants['leak-failed-creation'] = candidate.replace(
    'failed_create_dir:\n\tida_free(&cvp_debugfs_ids, idata->debugfs_id);', 'failed_create_dir:')
variants['unchecked-id-failure'] = candidate.replace('if (idata->debugfs_id < 0) {', 'if (0) {')
variants['early-id-release'] = candidate.replace(
    '\tdebugfs_remove_recursive(dentry);\n\tida_free(&cvp_debugfs_ids, idata->debugfs_id);',
    '\tida_free(&cvp_debugfs_ids, idata->debugfs_id);\n\tdebugfs_remove_recursive(dentry);')
variants['leak-removed-tree-id'] = candidate.replace(
    '\tdebugfs_remove_recursive(dentry);\n\tida_free(&cvp_debugfs_ids, idata->debugfs_id);',
    '\tdebugfs_remove_recursive(dentry);')
assert len(set(variants.values())) == len(variants)
results = {}
for name, source in variants.items():
    pair = re.search(r'struct cvp_core_inst_pair \{.*?\n\};', source, re.S).group()
    ida = 'static DEFINE_IDA(cvp_debugfs_ids);' if name != 'original' else ''
    code = adapter + pair + '\n' + ida + '\n'
    code += '\n'.join(function(source, n) for n in [
        'msm_cvp_debugfs_init_inst', 'msm_cvp_debugfs_deinit_inst']) + main
    path, exe = H / (name + '-test.c'), H / (name + '-test')
    path.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Werror',
                    '-Wno-unused-function', '-pthread', '-fsanitize=address,undefined',
                    str(path), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == (name == 'candidate'), (name, result.stderr)
    results[name] = {'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}

# File operations and permissions remain byte-identical. Kernel debugfs proxy
# serializes removal with file accesses; this patch keeps that API and ordering.
for name in ['inst_info_open', 'inst_info_read', 'inst_info_release']:
    pattern = r'^static [^\n]+ ' + name + r'\(.*?^\}'
    assert re.search(pattern, before, re.M | re.S).group() == re.search(pattern, candidate, re.M | re.S).group()
assert 'debugfs_create_file("info", 0444, dir,' in candidate
assert LIVE.read_text() in [before, candidate]
report = {
    'status': 'PASS', 'production_source': str(LIVE),
    'source_sha256': hashlib.sha256(candidate.encode()).hexdigest(),
    'before_sha256': hashlib.sha256(before.encode()).hexdigest(),
    'results': results, 'scenarios': 717, 'concurrent_instance_trees': 4096,
    'scope': 'Complete production debugfs init/deinit functions; allocation, ID, directory and file failures (NULL and ERR_PTR), multiple cores, simultaneous instances, INT_MAX IDs, ID reuse after removal and eight concurrent creators/removers. Original and five regression controls reject.',
    'limits': 'IDA and debugfs are host adapters using their allocation/removal contracts. Physical firmware sessions, real allocator pressure, debugfs access/removal races and on-device warning absence still need hardware verification.'
}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(results['candidate']['stdout'], end='')
print('Original and five regression controls rejected.')
