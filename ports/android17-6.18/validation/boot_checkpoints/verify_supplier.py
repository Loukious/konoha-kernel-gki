from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('--source-root', type=Path, required=True)
a = p.parse_args()
path = a.source_root / 'drivers/thermal/qcom/qti_devfreq_cdev.c'
text = path.read_text()
match = re.search(r'^static int devfreq_cdev_link_supplier\(.*?^\}', text, re.M | re.S)
assert match
source = match.group()
mutations = {
    'unbound_success': ('ret = device_is_bound(supplier) ? 0 : -EPROBE_DEFER;', 'ret = 0;'),
    'unlocked_bound_check': ('if (!device_trylock(supplier))', 'if (false)'),
    'supplier_lock_leak': ('device_unlock(supplier);', '(void)supplier;'),
    'lost_managed_lifetime': ('DL_FLAG_AUTOREMOVE_CONSUMER', 'DL_FLAG_STATELESS'),
}
clang = '/home/loukious/Android/konoha-android17-6.18-port-20261031/prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang'
results = []
with tempfile.TemporaryDirectory(prefix='onyx-cooling-supplier-') as tmp:
    root = Path(tmp)
    (root / 'test.c').write_bytes((HERE / 'tests/supplier.c').read_bytes())
    for name in ['production', *mutations]:
        code = source
        if name in mutations:
            old, new = mutations[name]
            assert code.count(old) == 1
            code = code.replace(old, new)
        (root / 'supplier.inc').write_text(code)
        build = subprocess.run([clang, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                                'test.c', '-o', name], cwd=root, capture_output=True, text=True)
        assert build.returncode == 0, (name, build.stderr)
        run = subprocess.run([str(root / name)], cwd=root, capture_output=True, text=True, timeout=10)
        assert (run.returncode == 0) == (name == 'production'), (name, run.stdout, run.stderr)
        results.append({'name': name, 'compiled': True, 'returncode': run.returncode,
                        'rejected': run.returncode != 0, 'output': (run.stdout + run.stderr)[-1000:]})
report = {'status': 'PASS', 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
          'scenarios': 5, 'results': results,
          'limits': 'Exact production supplier-link helper with driver-core link-state and device-lock mocks; real kernel probe/unbind scheduling and hardware remain runtime checks.'}
(HERE / 'host-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: five GPU supplier cases; four compiled regressions rejected')
