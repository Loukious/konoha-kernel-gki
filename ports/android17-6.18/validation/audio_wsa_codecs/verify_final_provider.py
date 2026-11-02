from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
IRQ = Path('/tmp/onyx-swr-irq-handoff')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
read = lambda name: json.loads((H / name).read_text())
old = read('validation.json')
assert (H / 'STATE.md').read_text().startswith('FROZEN')
assert (IRQ / 'STATE.md').read_text().startswith('FROZEN')
assert sha(H / 'patch') == old['patch_sha256']
for path, digest in read('frozen-hashes.json').items():
    assert sha(H / 'frozen-source' / path) == sha(W / 'onyx_audio_speakers' / path) == digest
for path, digest in old['shared_header_sha256'].items():
    assert sha(W / 'onyx' / path) == digest
provider_inputs = json.loads((IRQ / 'final-sha256.json').read_text())
for path, digest in provider_inputs.items():
    assert sha(IRQ / 'frozen-source' / path) == sha(W / 'onyx_swr_irq' / path) == digest
log = Path('/tmp/onyx-wsa-codecs-final-provider.log').read_text()
assert 'Build completed successfully' in log
assert not re.search(r'warning:|error:|WARNING:|ERROR:', log)
sys.path.insert(0, str(P))
from verify_modules import verify
modules = read('modules.json')
root = W / 'bazel-bin/onyx_audio_speakers'
paths = [root / item['target'] / item['output'] for item in modules]
providers = [W / 'bazel-bin/common/kernel_aarch64/Module.symvers']
providers += [W / 'bazel-bin' / package / target / (target + '_Module.symvers')
              for package, target in [('onyx_swr_irq', 'swr_dlkm'),
                                      ('onyx', 'wcd_core_dlkm'),
                                      ('onyx', 'qti_regmap_debugfs')]]
results = verify(paths, providers, old['kernel_release'])
for module, result, prior in zip(modules, results, old['module_verification']):
    target = module['target']
    for field in ['module', 'depends', 'aliases', 'imports_checked']:
        assert result[field] == prior[field], (target, field)
    exports = subprocess.check_output(['modprobe', '--show-exports', str(root / target / module['output'])], text=True).splitlines()
    assert sorted(line.split()[1] for line in exports) == sorted(line.split()[1] for line in prior['exports'])
    params = subprocess.check_output(['modinfo', '-F', 'parm', str(root / target / module['output'])], text=True).splitlines()
    assert params == prior['params']
    makefiles = root / (target + '_makefiles') / 'makefiles'
    assert sha(makefiles / 'Kbuild') == old['kbuild_sha256'][target]
    assert sha(makefiles / 'ddk_modinfo.c') == prior['ddk_generated_metadata_sha256']
    config = root / (target + '_config/out_dir/.config')
    assert sha(config) == old['config_sha256'][target]
    dest = H / 'final-provider-artifacts' / target
    dest.mkdir(parents=True, exist_ok=True)
    for path in [root / target / module['output'], root / target / (target + '_Module.symvers'),
                 makefiles / 'Kbuild', makefiles / 'ddk_modinfo.c',
                 makefiles / (target + '.cflags_shipped'), config]:
        shutil.copyfile(path, dest / path.name)
    result.update(exports=exports, params=params,
                  native_compilation_units=prior['native_compilation_units'])
report = {'status': 'PASS', 'supersedes': 'validation.json module artifacts and provider_input_sha256 only',
          'source_sha256': old['source_sha256'], 'patch_sha256': sha(H / 'patch'),
          'kernel_release': old['kernel_release'], 'module_verification': results,
          'final_irq_provider_sha256': provider_inputs,
          'irq_provider_validation_sha256': sha(IRQ / 'validation.json'),
          'build_log_sha256': sha(Path('/tmp/onyx-wsa-codecs-final-provider.log')),
          'limits': old['limits']}
(H / 'final-provider-verification.json').write_text(json.dumps(report, indent=2) + '\n')
shutil.copyfile('/tmp/onyx-wsa-codecs-final-provider.log', H / 'final-provider-build.log')
print('Verified frozen WSA codecs against final SoundWire provider:',
      sum(item['imports_checked'] for item in results), 'imported CRCs')
