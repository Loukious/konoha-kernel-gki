from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=H / 'private-source')
parser.add_argument('--output', type=Path, default=H / 'host-validation.json')
a = parser.parse_args()
path = a.source_root / 'drivers/iommu/arm/arm-smmu/arm-smmu.c'
text = path.read_text()
match = re.search(r'^static\nstruct arm_smmu_device \*arm_smmu_get_by_fwnode\(.*?\n\}', text, re.M | re.S)
assert match
source = match.group()
mutations = {
    'driver_bound_lookup': (
        'bus_find_device_by_fwnode(&platform_bus_type, fwnode)',
        'driver_find_device_by_fwnode(&arm_smmu_driver.driver, fwnode)'),
    'read_after_reference_release': (
        'struct arm_smmu_device *smmu = dev ? dev_get_drvdata(dev) : NULL;',
        'struct arm_smmu_device *smmu = NULL;'),
    'leaked_lookup_reference': ('\tput_device(dev);', '\t(void)dev;'),
}
clang = W / 'prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang'
results = []
with tempfile.TemporaryDirectory(prefix='onyx-smmu-lookup-') as tmp:
    root = Path(tmp)
    (root / 'test.c').write_bytes((H / 'tests/lookup.c').read_bytes())
    for name in ['production', *mutations]:
        code = source
        if name in mutations:
            old, new = mutations[name]
            assert code.count(old) == 1
            code = code.replace(old, new)
        if name == 'read_after_reference_release':
            code = code.replace('\treturn smmu;', '\tsmmu = dev ? dev_get_drvdata(dev) : NULL;\n\treturn smmu;')
        (root / 'production.inc').write_text(code)
        compile_result = subprocess.run([str(clang), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', 'test.c', '-o', name],
                       cwd=root, capture_output=True, text=True)
        assert compile_result.returncode == 0, (name, compile_result.stderr)
        run = subprocess.run([str(root / name)], cwd=root, capture_output=True, text=True, timeout=10)
        assert (run.returncode == 0) == (name == 'production'), (name, run.stdout, run.stderr)
        results.append({'name': name, 'compiled': True, 'rejected': run.returncode != 0,
                        'output': (run.stdout + run.stderr)[-2000:]})
report = {'status': 'PASS', 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
          'fixture_sha256': hashlib.sha256((H / 'tests/lookup.c').read_bytes()).hexdigest(),
          'scenarios': 5, 'results': results, 'negative_controls': results[1:],
          'limits': 'Exact production lookup function tested with kernel-list/reference mocks; full kernel probe and UFS enumeration still require hardware validation.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
print('PASS: five SMMU lookup scenarios; three compiled regressions rejected')
