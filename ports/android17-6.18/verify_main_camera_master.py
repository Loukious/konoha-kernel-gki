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
p = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_req_mgr/cam_req_mgr_dev.c'
s = p.read_text()
def fn(name):
    m = re.search(r'^(?:static )?(?:inline )?[\w *]+\b' + name + r'\([^;{]*\{', s, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[m.start():end] + '\n'
code = ''.join(fn(n) for n in ['cam_register_subdev', 'cam_unregister_subdev', 'cam_req_mgr_destroy_timer_slab', 'cam_req_mgr_component_master_bind', 'cam_req_mgr_component_master_unbind'])
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-master-') as td:
    d = Path(td)
    fragment = d / 'master.inc'
    fragment.write_text(code)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-Wno-unused-parameter', '-g',
           '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
           '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
           str(H / 'tests/main_camera_master.c'), '-o', str(d / 'master')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'master')], check=True, capture_output=True, text=True, timeout=20)
    result = json.loads(r.stdout)
    print(r.stdout.strip())
    if a.negative:
        variants = [
            ('timer_failure_success', 'rc = -ENOMEM;\n\t\tgoto slab_fail;', 'rc = 0;\n\t\tgoto slab_fail;'),
            ('registration_failure_falls_through', 'if (rc)\n\t\tgoto out;', 'if (false)\n\t\tgoto out;'),
            ('ordered_list_entry_leak', 'list_del_init(&csd->list);', '(void)csd;'),
            ('registration_count_underflow', 'if (csd->registered) {\n\t\tlist_del_init', 'if (true) {\n\t\tlist_del_init'),
            ('children_not_unbound_after_setup_error', 'vmrm_fail:\n\tcomponent_unbind_all(dev, NULL);', 'vmrm_fail:\n\t(void)dev;'),
            ('sysfs_failure_callback_leak', 'sysfs_fail:\n\tcam_vmrm_unregister_gh_callback();', 'sysfs_fail:\n\t(void)dev;'),
            ('premature_user_admission', 'g_dev.state = true;', 'g_dev.state = true;\n\tcam_nodes_ready = true;'),
            ('live_nodes_before_drain', 'cam_req_mgr_drain_nodes();\n\tsysfs_remove_file(&dev->kobj, &camera_debug_sysfs_attr.attr);\n\trc = cam_vmrm_unregister_gh_callback();', '(void)dev;\n\tsysfs_remove_file(&dev->kobj, &camera_debug_sysfs_attr.attr);\n\trc = cam_vmrm_unregister_gh_callback();'),
        ]
        for name, old, new in variants:
            assert old in code, name
            fragment.write_text(code.replace(old, new, 1))
            subprocess.run(cmd, check=True, capture_output=True)
            r = subprocess.run([str(d / 'master')], capture_output=True, timeout=20,
                               preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            assert r.returncode, name
            controls.append({'name': name, 'rejected': True, 'returncode': r.returncode})
            print('REJECTED', name)
            fragment.write_text(code)
report = {'status': 'PASS', 'tests': result, 'negative_controls': controls,
          'source_sha256': {str(p.relative_to(a.source_root)): hashlib.sha256(p.read_bytes()).hexdigest()},
          'harness_sha256': {'main_camera_master.c': hashlib.sha256((H / 'tests/main_camera_master.c').read_bytes()).hexdigest()},
          'limits': 'Actual master bind/unbind, timer-cache and subdevice registration functions; mocked setup, component and V4L2 services. VFS reference drain covered separately; no hardware or kernel execution.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
