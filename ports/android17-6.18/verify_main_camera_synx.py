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
ap.add_argument('--provider-root', type=Path, default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/onyx'))
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
base = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_sync'
provider = a.provider_root / 'qcom/opensource/synx-kernel/msm/synx'
sources = {n: (base / n).read_text() for n in ['cam_sync_synx.c', 'cam_sync_synx.h']}
providers = {n: (provider / n).read_text() for n in ['synx.c', 'synx_util.c']}
def bounds(s, name):
    m = re.search(r'^(?:static[ \t]+)?(?:inline[ \t]+)?(?:\w+[ \t]+)*\w+[ \t*]+(?:\n[ \t]*)?' + name + r'\([^;{]*\{', s, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return m.start(), end
def fn(s, name):
    start, end = bounds(s, name)
    return s[start:end] + '\n'
code = re.sub(r'^#include[^\n]*\n', '', sources['cam_sync_synx.c'], flags=re.M)
for name in ['__cam_synx_obj_get_monitor_entries', '__cam_synx_obj_get_prev_monitor_entries',
             '__cam_synx_obj_dump_monitor_array', '__cam_synx_obj_save_previous_monitor_data']:
    start, end = bounds(code, name)
    code = code[:start] + code[end:]
header = re.sub(r'^#include[^\n]*\n', '', sources['cam_sync_synx.h'], flags=re.M)
native = ''.join(fn(providers['synx.c'], n) for n in ['synx_internal_get_fence', 'synx_internal_wait', 'synx_internal_get_status'])
status = fn(providers['synx_util.c'], '__fence_state')
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-synx-') as td:
    d = Path(td)
    fragment = d / 'synx_camera.inc'
    fragment.write_text(code)
    (d / 'synx_header.inc').write_text(header)
    (d / 'synx_provider.inc').write_text(native)
    (d / 'synx_status.inc').write_text(status)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-Wno-unused-parameter', '-g', '-pthread', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
           '-I', str(d), str(H / 'tests/main_camera_synx.c'), '-o', str(d / 'synx')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'synx')], check=True, capture_output=True, text=True, timeout=25)
    result = json.loads(r.stdout)
    print(r.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('camera', 'error_pointer_session_accepted', 'if (IS_ERR_OR_NULL(session))', 'if (!session)'),
            ('camera', 'global_subscription_missing', 'rc = synx_wait(dev->session_handle, handle, 0);', 'rc = -ETIMEDOUT;'),
            ('camera', 'active_wait_treated_as_failure', 'if (rc < 0 && rc != -ETIMEDOUT)', 'if (rc < 0)'),
            ('camera', 'stale_cached_status', 'status = synx_wait(dev->session_handle, callback->synx_obj, 0);', 'status = synx_get_status(dev->session_handle, callback->synx_obj);'),
            ('camera', 'callback_not_published', 'row->callback = callback;', 'row->callback = NULL;'),
            ('camera', 'pending_callback_not_removed', 'dma_fence_remove_callback(callback->fence, &callback->fence_cb);', '(void)callback;'),
            ('camera', 'running_work_not_drained', 'cancel_work_sync(&callback->work);', '(void)callback;'),
            ('camera', 'backing_reference_leak', 'dma_fence_put(callback->fence);\n\t\tkfree(callback);', 'kfree(callback);'),
            ('camera', 'session_invalidated_before_close', 'cam_synx_obj_close();', '(void)dev;'),
            ('camera', 'failed_registration_published', 'row->cb_registered_for_sync = false;\n\t\tspin_unlock_bh', 'row->cb_registered_for_sync = true;\n\t\tspin_unlock_bh'),
            ('camera', 'provider_wait_in_irq_context', 'queue_work(callback->dev->work_queue, &callback->work);', '__cam_synx_obj_signal_work(&callback->work);'),
            ('provider', 'waiter_registration_removed', 'if (synx_util_is_global_handle(h_synx)) {', 'if (false) {'),
            ('provider', 'get_fence_reference_removed', 'dma_fence_get(fence);', '(void)fence;'),
            ('camera', 'incoming_callbacks_not_quiesced', 'void cam_synx_obj_quiesce(void)\n{', 'void cam_synx_obj_quiesce(void)\n{\n\treturn;'),
        ]
        for target, name, old, new in variants:
            original = code if target == 'camera' else native
            path = fragment if target == 'camera' else d / 'synx_provider.inc'
            # Withdrawal also occurs in quiesce; these controls alter the first release site.
            assert 1 <= original.count(old) <= 2, (name, original.count(old))
            path.write_text(original.replace(old, new, 1))
            subprocess.run(cmd, check=True, capture_output=True)
            try:
                r = subprocess.run([str(d / 'synx')], capture_output=True, timeout=10,
                                   preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert r.returncode, name
                returncode = r.returncode
            except subprocess.TimeoutExpired:
                returncode = 'bounded-deadlock-timeout'
            controls.append({'name': name, 'rejected': True, 'returncode': returncode})
            print('REJECTED', name, flush=True)
            path.write_text(original)
report = {'status': 'PASS', 'tests': result, 'negative_controls': controls,
          'source_sha256': {str((base / n).relative_to(a.source_root)): hashlib.sha256(s.encode()).hexdigest() for n, s in sources.items()},
          'provider_sha256': {str((provider / n).relative_to(a.provider_root)): hashlib.sha256(s.encode()).hexdigest() for n, s in providers.items()},
          'harness_sha256': {n: hashlib.sha256((H / 'tests' / n).read_bytes()).hexdigest() for n in ['main_camera_synx.c', 'main_camera_fence_mock.h']},
          'limits': 'Actual camera Synx functions plus native provider get-fence/wait/get-status/fence-status functions. Mocked session/global-state/DMA-fence/workqueue services, monitor and downstream sync-generation consumer. Native single-fence paths only; no kernel, IPC or hardware execution.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
