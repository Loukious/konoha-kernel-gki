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
probe = 'qcom/opensource/video-driver/driver/vidc/src/msm_vidc_probe.c'
header = 'qcom/opensource/video-driver/driver/vidc/inc/msm_vidc_core.h'

def extract(body, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', body)
    assert m, name
    pos = body.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (body[pos] == '{') - (body[pos] == '}')
        pos += 1
    return body[m.start():pos] + '\n'

names = ['msm_vidc_release_video_device', 'msm_vidc_unpublish_video_device', 'msm_vidc_wait_video_device', 'msm_vidc_unregister_video_device', 'msm_vidc_register_video_device', 'msm_vidc_initialize_media', 'msm_vidc_deinitialize_media']
source = '\n'.join(extract((root / probe).read_text(), name) for name in names)
layout = re.search(r'struct msm_video_device \{.*?\n\};', (root / header).read_text(), re.S).group()
controls = {
    'single_completion_consumed_twice': ('complete_all(&video->released);', 'complete(&video->released);'),
    'missing_video_release_wait': ('wait_for_completion(&video->released);', '(void)0;'),
    'missing_instance_drain': ('wait_event(core->instances_wait, !atomic_read(&core->live_instances));', '(void)0;'),
    'missing_release_queue_flush': ('flush_workqueue(core->release_workq);', '(void)0;'),
    'late_drvdata_publication': ('video_set_drvdata(&core->vdev[index].vdev, core);', '(void)0;'),
    'missing_partial_encoder_unwind': ('msm_vidc_unregister_video_device(core, MSM_VIDC_DECODER);', '(void)0;'),
    'missing_m2m_unwind': ('v4l2_m2m_release(core->vdev[index].m2m_dev);', '(void)0;'),
    'missing_m2m_unregister': ('v4l2_m2m_unregister_media_controller(video->m2m_dev);', '(void)0;'),
}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {x: sha(root / x) for x in [probe, header]}, 'harness_sha256': {'video_nodes.c': sha(H / 'tests/video_nodes.c')}}
with tempfile.TemporaryDirectory(prefix='video-nodes-test-') as tmp:
    for name in ['positive', *controls]:
        body = source
        if name in controls:
            before, after = controls[name]
            assert before in body, name
            body = body.replace(before, after, 1)
        path = Path(tmp) / 'video_nodes.c'
        path.write_text((H / 'tests/video_nodes.c').read_text().replace('/* SOURCE */', body).replace('/* LAYOUT */', layout))
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
            report['tests']['video_nodes'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Actual video node/media publication, final-release and cleanup functions with pthread V4L2/device/completion/workqueue service models. Tests retained files, delayed instance release, partial failures and rebind; no real VFS, V4L2 core or hardware execution.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
