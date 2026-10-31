import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
p.add_argument('--source-dir', default='onyx')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = a.workspace / a.source_dir
prefix = 'qcom/opensource/video-driver/driver/vidc/src/'
selection = {
    'msm_vidc_debug.c': ['msm_vidc_debugfs_deinit_inst'],
    'msm_vidc_driver.c': ['msm_vidc_free_inst', 'msm_vidc_release_instance', 'msm_vidc_close_helper', 'msm_vidc_init_inst_ref', 'msm_vidc_drain_inst_refs', 'get_inst_ref_locked', 'put_inst'],
}

def extract(body, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', body)
    assert m, name
    pos = body.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (body[pos] == '{') - (body[pos] == '}')
        pos += 1
    return body[m.start():pos] + '\n'

source = '\n'.join(extract((root / (prefix + file)).read_text(), name) for file, names in selection.items() for name in names)
controls = {
    'missing_failed_open_drain': ('wait_event(inst->ref_wait, kref_read(&inst->kref) == 1);', '(void)0;'),
    'lost_live_instance_count': ('atomic_inc(&core->live_instances);', '(void)0;'),
    'lost_live_instance_release': ('if (atomic_dec_and_test(&core->live_instances))', 'if (false)'),
    'synchronous_debugfs_destruction': ('queue_work(core->release_workq, &inst->release_work);', 'msm_vidc_release_instance(&inst->release_work);'),
    'lost_worker_final_put_drain': ('spin_lock_irqsave(&inst->ref_lock, flags);\n\tspin_unlock_irqrestore(&inst->ref_lock, flags);', '(void)flags;'),
    'lost_debugfs_drain': ('debugfs_remove_recursive(inst->debugfs_root);', '(void)0;'),
    'lost_debugfs_storage_release': ('vfree(inst->debugfs_data);', '(void)0;'),
    'lost_partial_unpublication': ('msm_vidc_remove_session(inst);\n\tmsm_vidc_remove_dangling_session(inst);\n\twait_event', 'wait_event'),
}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {prefix + x: sha(root / (prefix + x)) for x in selection}, 'harness_sha256': {'video_refs.c': sha(H / 'tests/video_refs.c')}}
with tempfile.TemporaryDirectory(prefix='video-refs-test-') as tmp:
    for name in ['positive', *controls]:
        body = source
        if name in controls:
            before, after = controls[name]
            assert before in body, name
            body = body.replace(before, after, 1)
        path = Path(tmp) / 'video_refs.c'
        path.write_text((H / 'tests/video_refs.c').read_text().replace('/* SOURCE */', body))
        binary = path.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(path), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        try:
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=8)
        except subprocess.TimeoutExpired:
            assert name != 'positive', name
            report['negative_controls'][name] = {'rejected': True, 'timeout': True}
            continue
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['instance_references'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Actual instance reference, partial-open drain, deferred destructor and debugfs teardown functions with pthread kref/workqueue/waitqueue/debugfs service models. Does not cover actual Linux scheduling, full open initialization, VFS/node teardown or firmware.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
