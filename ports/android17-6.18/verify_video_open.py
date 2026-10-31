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
path = 'qcom/opensource/video-driver/driver/vidc/src/msm_vidc.c'
body = (root / path).read_text()
source = body[body.index('void *msm_vidc_open('):body.index('\nint msm_vidc_close(')]
controls = {
    'early_session_publication': ('rc = msm_vidc_pools_init(inst);', 'msm_vidc_add_session(inst);\n\trc = msm_vidc_pools_init(inst);'),
    'missing_partial_reference_drain': ('msm_vidc_drain_inst_refs(inst);', '(void)0;'),
    'missing_partial_codec_cleanup': ('msm_vdec_inst_deinit(inst);', '(void)0;'),
    'missing_vb2_cleanup': ('msm_vidc_vb2_queue_deinit(inst);', '(void)0;'),
    'missing_fh_cleanup': ('msm_vidc_v4l2_fh_deinit(inst);', '(void)0;'),
    'missing_workqueue_cleanup': ('destroy_workqueue(inst->workq);', '(void)0;'),
    'missing_pool_cleanup': ('msm_vidc_pools_deinit(inst);', '(void)0;'),
    'missing_session_error_core_cleanup': ('msm_vidc_core_deinit(core, true);', '(void)0;'),
    'missing_completion_initialization': ('init_completion(&inst->completions[i]);', '(void)0;'),
    'missing_buffer_list_initialization': ('INIT_LIST_HEAD(&inst->buffers.input.list);', '(void)0;'),
}
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest()}, 'harness_sha256': {'video_open.c': hashlib.sha256((H / 'tests/video_open.c').read_bytes()).hexdigest()}}
with tempfile.TemporaryDirectory(prefix='video-open-test-') as tmp:
    for name in ['positive', *controls]:
        current = source
        if name in controls:
            before, after = controls[name]
            assert before in current, name
            current = current.replace(before, after)
        f = Path(tmp) / 'open.c'
        f.write_text((H / 'tests/video_open.c').read_text().replace('/* SOURCE */', current))
        binary = f.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(f), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['open_initialization'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Full production msm_vidc_open with allocation/core/codec/queue/fence/debugfs models. Checks both domains, all modeled acquisition failures, partial codec initialization and initialized publication; reference-drain concurrency is tested separately. No actual firmware or codec initialization execution.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
