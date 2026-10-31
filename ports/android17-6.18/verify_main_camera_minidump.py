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
p = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_utils/cam_common_util.c'
s = p.read_text()

def fn(name):
    m = re.search(r'^(?:static[ \t]+)?(?:inline[ \t]+)?(?:\w+[ \t]+)*\w+[ \t*]+(?:\n[ \t]*)?' + name + r'\([^;{]*\{', s, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[m.start():end] + '\n'

start = s.index('struct cam_common_mini_dump_entry {')
code = s[start:s.index('#endif', start)]
code += fn('cam_common_mini_dump_handler') + fn('cam_common_md_notify_handler')
start = s.index('static struct notifier_block cam_common_md_notify_blk =')
code += s[start:s.index('};', start) + 2] + '\n'
code += fn('cam_common_register_mini_dump_cb') + fn('cam_common_unregister_mini_dump_cb')
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-minidump-') as td:
    d = Path(td)
    fragment = d / 'minidump_camera.inc'
    fragment.write_text(code)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
           '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
           '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
           str(H / 'tests/main_camera_minidump.c'), '-o', str(d / 'minidump')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'minidump')], check=True, capture_output=True, text=True, timeout=20)
    result = json.loads(r.stdout)
    print(r.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('reader_lifetime_unprotected', 'rcu_read_lock();', '(void)md;'),
            ('remaining_length_not_checked', 'if (written > remaining)', 'if (written > len)'),
            ('exact_capacity_overrun', 'i < CAM_COMMON_MINI_DUMP_DEV_NUM && remaining', 'i < CAM_COMMON_MINI_DUMP_DEV_NUM'),
            ('duplicate_entry_consumes_slot', 'if (existing && existing->callback == mini_dump_cb &&\n\t\t\texisting->priv_data == priv_data)', 'if (false)'),
            ('provider_failure_published', 'if (rc)\n\t\t\tgoto out;', 'if (false)\n\t\t\tgoto out;'),
            ('removed_entry_left_visible', 'RCU_INIT_POINTER(cam_mini_dump_entries[i], NULL);', '(void)i;'),
            ('callback_drain_removed', 'synchronize_rcu();', '(void)i;'),
            ('empty_registry_still_subscribed', 'qcom_va_md_unregister("Camera", &cam_common_md_notify_blk);', '(void)i;'),
            ('slot_count_never_released', 'cam_mini_dump_count--;', '(void)i;'),
            ('failed_entry_leaked', 'kfree(entry);\n\treturn rc;', '(void)entry;\n\treturn rc;'),
        ]
        for name, old, new in variants:
            assert old in code, name
            modified = code.replace(old, new, 1)
            if name == 'reader_lifetime_unprotected':
                modified = modified.replace('rcu_read_unlock();', '(void)md;', 1)
            fragment.write_text(modified)
            subprocess.run(cmd, check=True, capture_output=True)
            try:
                r = subprocess.run([str(d / 'minidump')], capture_output=True, timeout=10,
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
          'harness_sha256': {'main_camera_minidump.c': hashlib.sha256((H / 'tests/main_camera_minidump.c').read_bytes()).hexdigest()},
          'limits': 'Actual camera registry, notification and capture functions. Pthread grace periods and mocked VA-minidump provider; no real panic, kernel RCU or hardware execution. Caller ownership is validated separately.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
