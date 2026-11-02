"""Freeze the separate provider patch after exact replay and artifact checks.

Run under flock /tmp/onyx-6.18-build.lock. Never use to restage central source.
"""
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
D = W / 'onyx_extdisp_audio'
B = W / 'bazel-bin/onyx_extdisp_audio'
paths = json.loads((H / 'sources.json').read_text())
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert not (H / 'frozen-source').exists(), 'Already frozen; do not overwrite.'
hashes = {path: sha(H / 'private-source' / path) for path in paths}
original = json.loads((H / 'original-sha256.json').read_text())
for path in paths:
    assert sha(D / path) == hashes[path], path
    assert sha(H / 'original' / path) == original[path], path
    assert sha(W / 'onyx' / path) == original[path], path

subprocess.run([sys.executable, str(H / 'verify_extdisp.py'), '--negative',
    '--source-root', str(D), '--consumer-root', str(W / 'onyx_audio_small'),
    '--output', str(H / 'staged-validation.json')], check=True)
host = json.loads((H / 'staged-validation.json').read_text())
assert host['source_sha256'] == hashes
assert len(host['negative_controls']) == 7
with tempfile.TemporaryDirectory(prefix='onyx-extdisp-audio-replay-') as temporary:
    replay = Path(temporary)
    shutil.copytree(H / 'original', replay, dirs_exist_ok=True)
    run = subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(H / 'patch')],
                         cwd=replay, capture_output=True, text=True, check=True)
    output = run.stdout + run.stderr
    assert not re.search(r'fuzz|offset|FAILED', output, re.I)
    assert {path: sha(replay / path) for path in paths} == hashes
    (H / 'replay.log').write_text(output)

log = Path('/tmp/onyx-audio-small-handoff/build-five-final.log').read_text()
assert 'Build completed successfully' in log
assert not re.search(r'warning:|error:|WARNING:|ERROR:', log)
(H / 'build.log').write_text(log)
check_cmd = [str(W / 'common/scripts/checkpatch.pl'), '--no-tree', '--strict', str(H / 'patch')]
check = subprocess.run(check_cmd, capture_output=True, text=True)
raw = check.stdout + check.stderr
assert '1 errors, 0 warnings, 0 checks' in raw
assert 'ERROR: Missing Signed-off-by: line(s)' in raw
(H / 'checkpatch.log').write_text(raw)
code_check = subprocess.run(check_cmd[:-1] + ['--no-signoff', check_cmd[-1]], capture_output=True, text=True)
assert not code_check.returncode, code_check.stdout + code_check.stderr
(H / 'checkpatch-code.log').write_text(code_check.stdout + code_check.stderr)

sys.path.insert(0, str(P))
from verify_modules import verify

module = B / 'msm_ext_display/unstripped/msm_ext_display.ko'
symvers = B / 'msm_ext_display/msm_ext_display_Module.symvers'
common_symvers = W / 'bazel-bin/common/kernel_aarch64/Module.symvers'
release = (W / 'bazel-bin/common/kernel_aarch64/include/config/kernel.release').read_text().strip()
checked = verify([module], [common_symvers], release)[0]
old_symvers = W / 'bazel-bin/onyx/msm_ext_display/msm_ext_display_Module.symvers'

def exports(path):
    return {line.split()[1]: line.split() for line in path.read_text().splitlines()}

old, new = exports(old_symvers), exports(symvers)
assert all(new.get(name) == fields for name, fields in old.items())
assert set(new) - set(old) == {'msm_ext_disp_unregister_audio_codec'}
assert checked['depends'] == ''
old_module = W / 'bazel-bin/onyx/msm_ext_display/unstripped/msm_ext_display.ko'
for field in ('alias', 'parm', 'license', 'description'):
    before = subprocess.check_output(['modinfo', '-F', field, str(old_module)], text=True)
    after = subprocess.check_output(['modinfo', '-F', field, str(module)], text=True)
    assert before == after, field

header_path = 'qcom/opensource/mm-drivers/msm_ext_display/include/msm_ext_display.h'
for name in ['msm_ext_disp_audio_codec_ops', 'msm_ext_disp_data', 'msm_ext_disp_init_data']:
    pattern = r'struct ' + name + r' \{.*?\n\};'
    assert re.search(pattern, (H / 'original' / header_path).read_text(), re.S)[0] == \
           re.search(pattern, (D / header_path).read_text(), re.S)[0]

makefiles = B / 'msm_ext_display_makefiles/makefiles'
kbuild = (makefiles / 'Kbuild').read_text()
assert re.findall(r'^msm_ext_display-y \+= (.*)$', kbuild, re.M) == \
    ['qcom/opensource/mm-drivers/msm_ext_display/src/msm_ext_display.o']
config = B / 'msm_ext_display_config/out_dir/.config'
for line in ['CONFIG_MODULES=y', 'CONFIG_MODVERSIONS=y', 'CONFIG_OF=y', 'CONFIG_EXTCON=y']:
    assert line in config.read_text().splitlines(), line

shutil.copytree(H / 'private-source', H / 'frozen-source')
artifacts = H / 'artifacts'
artifacts.mkdir(exist_ok=True)
for src, name in [(module, 'msm_ext_display.ko'), (symvers, 'msm_ext_display_Module.symvers'),
                  (old_symvers, 'prior_Module.symvers'), (common_symvers, 'kernel_Module.symvers'),
                  (config, 'module.config')]:
    shutil.copyfile(src, artifacts / name)
shutil.copytree(makefiles, artifacts / 'makefiles')
(H / 'imports.json').write_text('[]\n')
(H / 'frozen-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
report = {'status': 'FROZEN', 'module_verification': checked, 'kernel_release': release,
    'existing_export_crcs_preserved': old, 'new_exports': sorted(set(new) - set(old)),
    'original_sha256': original, 'source_sha256': hashes, 'patch_sha256': sha(H / 'patch'),
    'build_log_sha256': sha(H / 'build.log'), 'kbuild_sha256': sha(makefiles / 'Kbuild'),
    'config_sha256': sha(config), 'artifact_sha256': {str(p.relative_to(artifacts)): sha(p)
        for p in sorted(artifacts.rglob('*')) if p.is_file()},
    'replay': 'zero fuzz, zero offsets, exact source hashes', 'host_tests': host,
    'checkpatch': {'unfiltered_exit': check.returncode, 'unfiltered_remaining': 'Missing Signed-off-by only',
                   'source_style_exit': code_check.returncode, 'source_style': '0 errors, 0 warnings, 0 checks'},
    'limits': ['Permanent display provider binding remains the integrated contract.',
        'The caller must quiesce its own ASoC operations before withdrawal.',
        'Independent display ready callbacks may complete after withdrawal; they retain no codec-owned table.',
        'No physical HDMI/DP, kernel extcon/ASoC integration, supplier unbind or phone boot validation.']}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
(H / 'STATE.md').write_text('FROZEN separate display codec-storage withdrawal provider. Two existing integrated '
    'paths, no fresh imports/module names. Existing four export CRCs preserved, one new withdrawal export; '
    f'{checked["imports_checked"]} imported CRCs verified. Provider+HDMI suites and seven compiled negative '
    'controls pass against staged source. Zero-fuzz/zero-offset replay and exact staged/header/structure hashes '
    'verified. Source style clean; unfiltered unsigned-patch check retains only the Missing Signed-off-by '
    'administrative error. Root integrates this patch before the small-audio/HDMI patch; standalone BUILD '
    'fragment is not appended to the existing central target. Do not rerun import, stage or freeze after '
    'integration. See validation.json and README.md.\n')
print('FROZEN display provider:', checked['imports_checked'], 'imported CRCs; 4 existing export CRCs retained.')
