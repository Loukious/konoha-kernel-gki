from pathlib import Path
import argparse
import hashlib
import json
import re
import resource
import subprocess
import tempfile

H = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--source-root', type=Path, default=H / 'private-source')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
p = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_sync/cam_sync.c'
s = p.read_text()
def fn(name):
    m = re.search(r'^(?:static[ \t]+)?(?:inline[ \t]+)?(?:\w+[ \t]+)*\w+[ \t*]+(?:\n[ \t]*)?' + name + r'\([^;{]*\{', s, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[m.start():end] + '\n'
code = ''.join(fn(n) for n in ['cam_sync_v4l2_released', 'cam_sync_drain_video',
                              'cam_sync_component_bind', 'cam_sync_component_unbind',
                              'cam_sync_open', 'cam_sync_close'])
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-sync-life-') as td:
    d = Path(td)
    fragment = d / 'sync_lifecycle.inc'
    fragment.write_text(code)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-Wno-unused-parameter', '-g', '-pthread', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
           '-I', str(d), str(H / 'tests/main_camera_sync_lifecycle.c'), '-o', str(d / 'life')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'life')], check=True, capture_output=True, text=True, timeout=20)
    result = json.loads(r.stdout)
    print(r.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('early_node_publication', '/* Initialize dma fence driver */', 'video_register_device(sync_dev->vdev, VFL_TYPE_VIDEO, -1);'),
            ('table_lock_held_during_callback_drain', 'mutex_lock(&sync_dev->file_lock);\n\tsync_dev->open_cnt--;', 'mutex_lock(&sync_dev->table_lock);\n\tsync_dev->open_cnt--;'),
            ('dma_producers_not_drained', 'cam_dma_fence_quiesce();', '(void)sync_dev;'),
            ('synx_producers_not_drained', 'cam_synx_obj_quiesce();', '(void)sync_dev;'),
            ('downstream_callbacks_not_drained', 'flush_workqueue(sync_dev->work_queue);', '(void)sync_dev;'),
            ('video_references_not_drained', 'cam_sync_drain_video(sync_dev);', '(void)sync_dev;'),
            ('workqueue_leaked_on_setup_error', 'destroy_workqueue(sync_dev->work_queue);', '(void)sync_dev;'),
            ('node_reference_leaked_on_fh_failure', 'if (rc)\n\t\tcam_req_mgr_put_node();', '(void)rc;'),
            ('second_open_admitted', 'if (sync_dev->open_cnt >= 1)', 'if (sync_dev->open_cnt >= 2)'),
            ('event_handle_not_withdrawn', 'sync_dev->cam_sync_eventq = NULL;', '(void)sync_dev;'),
        ]
        for name, old, new in variants:
            assert old in code, name
            fragment.write_text(code.replace(old, new, 1))
            subprocess.run(cmd, check=True, capture_output=True)
            try:
                r = subprocess.run([str(d / 'life')], capture_output=True, timeout=10,
                                   preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert r.returncode, name
                returncode = r.returncode
            except subprocess.TimeoutExpired:
                returncode = 'bounded-deadlock-timeout'
            controls.append({'name': name, 'rejected': True, 'returncode': returncode})
            print('REJECTED', name, flush=True)
            fragment.write_text(code)
report = {'status': 'PASS', 'tests': result, 'negative_controls': controls,
          'source_sha256': {str(p.relative_to(a.source_root)): hashlib.sha256(p.read_bytes()).hexdigest()},
          'harness_sha256': {n: hashlib.sha256((H / 'tests' / n).read_bytes()).hexdigest() for n in ['main_camera_sync_lifecycle.c', 'main_camera_fence_mock.h']},
          'limits': 'Actual sync component bind/unbind and file open/close, with real pthread callback queue. Mocked V4L2, allocation, media and fence-provider services; no native VFS, kernel or hardware execution.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
