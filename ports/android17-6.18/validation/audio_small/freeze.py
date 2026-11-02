"""Freeze six native audio modules after the final strict build.

Invoke under flock /tmp/onyx-6.18-build.lock. No central source is modified.
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
D = W / 'onyx_audio_small'
assert not (H / 'frozen-source').exists(), 'Already frozen; do not overwrite.'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
paths = json.loads((H / 'sources.json').read_text())
hashes = {path: sha(H / 'private-source' / path) for path in paths}
for path, digest in hashes.items():
    assert sha(D / path) == digest, path

log = (H / 'build-all-final.log').read_text()
assert 'Build completed successfully' in log
assert not re.search(r'warning:|error:|WARNING:|ERROR:', log)
subprocess.run([sys.executable, str(H / 'verify_small.py'), '--source-root', str(D),
    '--negative', '--output', str(H / 'staged-validation.json')], check=True)
tests = json.loads((H / 'staged-validation.json').read_text())
assert tests['source_sha256'] == hashes
assert set(tests['tests']) == {'sdca', 'lpi', 'dmic', 'haptics'}
assert len(tests['negative_controls']) == 17
assert all(item['rejected'] for item in tests['negative_controls'].values())
subprocess.run([sys.executable, str(H / 'verify_artifacts.py'), '--capture'], check=True)
modules = json.loads((H / 'module-verification.json').read_text())
assert len(modules['modules']) == 6
assert modules['source_sha256'] == hashes

pins = json.loads((H / 'imports.json').read_text())
with tempfile.TemporaryDirectory(prefix='onyx-small-audio-replay-') as temporary:
    replay = Path(temporary)
    for pin in pins:
        data = subprocess.check_output(['git', '-C', str(W / pin['project']), 'cat-file', 'blob', pin['blob']])
        dest = replay / pin['path']
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        assert data == (H / 'original' / pin['path']).read_bytes()
    run = subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(H / 'patch')],
                         cwd=replay, capture_output=True, text=True, check=True)
    output = run.stdout + run.stderr
    assert not re.search(r'fuzz|offset|FAILED', output, re.I)
    assert {path: sha(replay / path) for path in paths} == hashes
    (H / 'replay.log').write_text(output)

shared = {}
existing = set()
for name in modules['modules']:
    existing.update(json.loads((H / (name + '.json')).read_text())['existing_paths'])
for path in sorted(existing):
    package = 'onyx'
    if path == 'qcom/opensource/audio-kernel/include/soc/soundwire.h':
        package = 'onyx_swr_irq'
    elif path == 'qcom/opensource/mm-drivers/msm_ext_display/include/msm_ext_display.h':
        package = 'onyx_extdisp_audio'
    shared[path] = {'package': package, 'sha256': sha(W / package / path)}
wcd = 'qcom/opensource/audio-kernel/asoc/codecs/wcd939x/wcd939x.h'
shared[wcd] = {'package': 'onyx_wcd_codecs', 'sha256': sha(W / 'onyx_wcd_codecs' / wcd)}
assert shared['qcom/opensource/audio-kernel/include/soc/soundwire.h']['sha256'] == \
    'd81a764fef4407a483c5db2c4ed6290b1992b7f00b6ac004dbf3376ec2b1fda9'
frozen_extdisp = json.loads(Path('/tmp/onyx-extdisp-audio-lifetime-handoff/frozen-hashes.json').read_text())
for path, digest in frozen_extdisp.items():
    assert sha(W / 'onyx_extdisp_audio' / path) == digest, path
for path, digest in json.loads((H / 'shared-inputs-initial.json').read_text()).items():
    allowed = {digest}
    if shared[path]['package'] != 'onyx':
        allowed.add(shared[path]['sha256'])
    assert sha(W / 'onyx' / path) in allowed, path

check_cmd = [str(W / 'common/scripts/checkpatch.pl'), '--no-tree', '--strict', str(H / 'patch')]
check = subprocess.run(check_cmd, capture_output=True, text=True)
raw = check.stdout + check.stderr
assert '1 errors, 0 warnings, 0 checks' in raw and 'ERROR: Missing Signed-off-by: line(s)' in raw
(H / 'checkpatch.log').write_text(raw)
code_check = subprocess.run(check_cmd[:-1] + ['--no-signoff', check_cmd[-1]], capture_output=True, text=True)
assert not code_check.returncode, code_check.stdout + code_check.stderr
(H / 'checkpatch-code.log').write_text(code_check.stdout + code_check.stderr)

shutil.copytree(H / 'private-source', H / 'frozen-source')
(H / 'frozen-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
(H / 'shared-header-hashes.json').write_text(json.dumps(shared, indent=2) + '\n')
for path, record in shared.items():
    dest = H / 'shared-snapshot' / record['package'] / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(W / record['package'] / path, dest)
report = {'status': 'FROZEN', 'modules': 6, 'imports': len(pins),
    'imported_crcs_checked': modules['imports_checked'],
    'module_verification': modules, 'host_validation': tests,
    'source_sha256': hashes, 'shared_headers': shared, 'extdisp_provider_sources': frozen_extdisp,
    'patch_sha256': sha(H / 'patch'), 'build_sha256': sha(H / 'build-all-final.log'),
    'replay': 'zero fuzz, zero offsets, exact source hashes',
    'checkpatch': {'unfiltered_exit': check.returncode, 'unfiltered_remaining': 'Missing Signed-off-by only',
                   'source_style_exit': code_check.returncode, 'source_style': '0 errors, 0 warnings, 0 checks'},
    'dt_evidence_sha256': sha(H / 'dt-evidence.json'),
    'limits': ['Only LPI, native stub and HDMI have enabled nodes in the archived checked DT variants.',
        'DMIC and haptics are retained ROM alternatives; SDCA is a service.',
        'The native stub hardware-parameter driver is retained, including the AW882XX branch.',
        'Host tests mock kernel services; no real ASoC/DAPM, debugfs proxy, IRQ, DSP, PM, physical audio or boot validation.',
        'Debugfs owners must remove SDCA entries before freeing regdump/component storage.',
        'Supplier links and component/notifier teardown ordering need real kernel unbind/SSR validation.',
        'Provider packages may be integrated separately; rerun CRC and shared-header checks after integration.']}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
(H / 'STATE.md').write_text('FROZEN: six native one-object small audio modules; ten pinned fresh imports. '
    f'{modules["imports_checked"]} imported CRCs and 31 native tables checked; native exports, aliases, '
    'dependencies, parameters and eight feature selections preserved. Four compiled host suites plus '
    '17 rejecting controls pass against exact staged source. Source/shared/header/build hashes and '
    'zero-fuzz/zero-offset replay verified. Unfiltered strict checkpatch retains only the unsigned '
    'handoff missing-signoff error; source style has no warnings/checks. Root integrates after frozen '
    'SoundWire IRQ and extdisp provider changes, with the WCD codec header/provider dependencies. '
    'No phone audio/boot claim. Do not rerun import/adapt/stage/freeze after integration. '
    'See README.md, integration-dependencies.json and validation.json.\n')
print('FROZEN small audio:', modules['imports_checked'], 'imported CRCs; 6 modules, 17 rejecting controls.')
