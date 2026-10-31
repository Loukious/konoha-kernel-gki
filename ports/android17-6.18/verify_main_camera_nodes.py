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
ap.add_argument('--output', required=True, type=Path)
a = ap.parse_args()
source = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_req_mgr/cam_req_mgr_dev.c'
s = source.read_text()
def fn(name):
    m = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;{]*\{', s, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[m.start():end] + '\n'
names = ['cam_req_mgr_get_node', 'cam_req_mgr_put_node', 'cam_v4l2_device_released',
         'cam_subdev_open_tracked', 'cam_subdev_close_tracked', 'cam_req_mgr_drain_nodes']
code = ''.join(fn(name) for name in names)
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-nodes-') as td:
    d = Path(td)
    fragment = d / 'nodes.inc'
    fragment.write_text(code)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
           '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
           '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
           str(H / 'tests/main_camera_nodes.c'), '-o', str(d / 'nodes')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'nodes')], text=True, capture_output=True, check=True, timeout=30)
    results = json.loads(r.stdout)
    print(r.stdout.strip())
    if a.negative:
        mutations = [
            ('helper_failure_double_withdrawal', 'if (cam_subdev_nodes_live)', 'if (true)'),
            ('open_before_ready', 'if (!cam_nodes_ready)', 'if (false)'),
            ('failed_open_ownership_leak', 'if (rc < 0)\n\t\tcam_req_mgr_put_node();', 'if (rc < 0)\n\t\t(void)rc;'),
            ('positive_open_count_lost', 'if (rc < 0)', 'if (rc)'),
            ('close_ownership_leak', '\tcam_req_mgr_put_node();\n\treturn rc;\n}\nstatic void cam_req_mgr_drain_nodes', '\treturn rc;\n}\nstatic void cam_req_mgr_drain_nodes'),
            ('files_not_drained', 'wait_event(cam_node_wait, !READ_ONCE(cam_node_users));', '(void)cam_node_wait;'),
            ('vfs_tail_not_drained', 'wait_for_completion(&cam_v4l2_released);', '(void)cam_v4l2_released;'),
        ]
        for name, before, after in mutations:
            assert before in code, name
            fragment.write_text(code.replace(before, after, 1))
            subprocess.run(cmd, check=True, capture_output=True)
            try:
                r = subprocess.run([str(d / 'nodes')], capture_output=True, timeout=10,
                                   preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert r.returncode, name
                reason = {'returncode': r.returncode}
            except subprocess.TimeoutExpired:
                reason = {'deadlocked': True}
            controls.append({'name': name, 'rejected': True, **reason})
            print('REJECTED', name)
            fragment.write_text(code)
report = {'status': 'PASS', 'tests': results, 'negative_controls': controls,
          'source_sha256': {str(source.relative_to(a.source_root)): hashlib.sha256(source.read_bytes()).hexdigest()},
          'harness_sha256': {'main_camera_nodes.c': hashlib.sha256((H / 'tests/main_camera_nodes.c').read_bytes()).hexdigest()},
          'limits': 'Actual admission, subdevice callbacks and drain functions; mocked V4L2 references and pthread services. Master setup, sync teardown and real kernel VFS are not executed.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
