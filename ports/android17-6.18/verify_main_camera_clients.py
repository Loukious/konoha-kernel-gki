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
base = a.source_root / 'qcom/opensource/camera-kernel/drivers'
paths = {'jpeg': base / 'cam_jpeg/cam_jpeg_dev.c', 'manager': base / 'cam_jpeg/jpeg_hw/cam_jpeg_hw_mgr.c', 'isp': base / 'cam_isp/cam_isp_dev.c'}
sources = {name: p.read_text() for name, p in paths.items()}

def fn(source, name):
    m = re.search(r'^(?:static[ \t]+)?(?:inline[ \t]+)?(?:\w+[ \t]+)*\w+[ \t*]+(?:\n[ \t]*)?' + name + r'\([^;{]*\{', source, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[m.start():end] + '\n'

code = fn(sources['manager'], 'cam_jpeg_hw_mgr_deinit')
for name in ['cam_jpeg_dev_component_bind', 'cam_jpeg_dev_component_unbind']:
    code += fn(sources['jpeg'], name)
for name in ['cam_isp_dev_component_bind', 'cam_isp_dev_component_unbind']:
    code += fn(sources['isp'], name)
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-clients-') as td:
    d = Path(td)
    fragment = d / 'clients_camera.inc'
    fragment.write_text(code)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
           '-Wno-sign-compare', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
           '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d), str(H / 'tests/main_camera_clients.c'), '-o', str(d / 'clients')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'clients')], check=True, capture_output=True, text=True, timeout=20)
    result = json.loads(r.stdout)
    print(r.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('jpeg_partial_init_manager_leak', 'ctx_init_fail:\n\tcam_jpeg_hw_mgr_deinit();', 'ctx_init_fail:'),
            ('jpeg_only_eight_contexts_drained', 'for (i = 0; i < CAM_JPEG_CTX_MAX; i++) {\n\t\trc = cam_jpeg_context_deinit', 'for (i = 0; i < CAM_CTX_MAX; i++) {\n\t\trc = cam_jpeg_context_deinit'),
            ('jpeg_workqueue_leak', 'cam_req_mgr_workq_destroy(&g_jpeg_hw_mgr.work_process_frame);', '(void)i;'),
            ('jpeg_dump_left_registered', 'cam_common_unregister_mini_dump_cb(cam_jpeg_hw_mgr_mini_dump_cb, NULL);', '(void)cam_jpeg_hw_mgr_mini_dump_cb;'),
            ('jpeg_fault_left_registered', 'cam_smmu_remove_client(g_jpeg_dev.sd.token);', '(void)dev;'),
            ('isp_error_overwritten', 'unregister:\n\tcam_subdev_remove(&g_isp_dev.sd);', 'unregister:\n\trc = cam_subdev_remove(&g_isp_dev.sd);'),
            ('isp_allocation_false_success', 'rc = -ENOMEM;\n\t\tgoto unregister;', 'rc = 0;\n\t\tgoto unregister;'),
            ('isp_partial_contexts_leaked', 'while (i-- > 0)', 'while (false)'),
            ('isp_bound_manager_leaked', 'if (hw_ready) {', 'if (hw_ready && false) {'),
            ('isp_fault_left_registered', 'cam_smmu_remove_client(g_isp_dev.sd.token);', '(void)dev;'),
        ]
        for name, old, new in variants:
            assert old in code, name
            fragment.write_text(code.replace(old, new, 1))
            subprocess.run(cmd, check=True, capture_output=True)
            r = subprocess.run([str(d / 'clients')], capture_output=True, timeout=20,
                               preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            assert r.returncode, name
            controls.append({'name': name, 'rejected': True, 'returncode': r.returncode})
            print('REJECTED', name, flush=True)
            fragment.write_text(code)
report = {'status': 'PASS', 'tests': result, 'negative_controls': controls,
          'source_sha256': {str(p.relative_to(a.source_root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths.values()},
          'harness_sha256': {'main_camera_clients.c': hashlib.sha256((H / 'tests/main_camera_clients.c').read_bytes()).hexdigest()},
          'limits': 'Actual JPEG/ISP component bind/unbind and JPEG manager destructor. Mocked hardware-manager setup, providers, contexts and drained workqueues. No native IRQ, device or hardware execution; source context counts modeled as JPEG64/IFE32.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
