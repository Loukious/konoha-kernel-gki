from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

H = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('workspace', type=Path)
parser.add_argument('--source-dir', default='onyx_swr_irq')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--negative', action='store_true')
args = parser.parse_args()
S = args.workspace / args.source_dir / 'qcom/opensource/audio-kernel'
provider = S / 'soc/soundwire.c'
controller = S / 'soc/swr-mstr-ctrl.c'
header = S / 'include/soc/soundwire.h'
source = provider.read_text()
begin = source.index('static int swr_master_begin(')
end = source.index('struct swr_transaction {', begin)
production = source[begin:end]
ctrl = controller.read_text()
start = ctrl.index('static void swrm_handle_nested_irq(')
stop = ctrl.index('static irqreturn_t swr_mstr_interrupt(', start)
production += ctrl[start:stop]
assert 'swrm_dispatch_slave_irq(swrm, devnum);' in ctrl
interrupt = ctrl[stop:ctrl.index('\nstatic ', stop + 1)]
assert '->slave_irq' not in interrupt and 'handle_nested_irq(' not in interrupt
for struct in ['swr_device', 'swr_master']:
    pattern = r'struct ' + struct + r' \{.*?\n\};'
    assert re.search(pattern, header.read_text(), re.S).group() == re.search(
        pattern, (H / 'original/qcom/opensource/audio-kernel/include/soc/soundwire.h').read_text(), re.S).group()

mutations = {
    'missing_drain_lock': ('mutex_lock(&found->dispatch_lock);\n\tfound->domain = NULL;\n\tWRITE_ONCE(swr->slave_irq, NULL);\n\tmutex_unlock(&found->dispatch_lock);',
                           'found->domain = NULL;\n\tWRITE_ONCE(swr->slave_irq, NULL);'),
    'missing_device_reference': ('get_device(&swr->dev);', '(void)swr;'),
    'missing_dispatch_reference': ('refcount_inc(&irq->refs);', '(void)irq;'),
    'missing_self_rejection': ('if (swr_slave_irq_in_handler()) {', 'if (false) {'),
    'dispatch_after_withdrawal': ('if (found->domain) {', 'if (true) {'),
    'missing_controller_up_check': ('pending && READ_ONCE(swrm->dev_up)', 'pending'),
}
results = []
clang = args.workspace / 'prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang'
assert clang.is_file(), 'Pinned Clang r584948c is required'
with tempfile.TemporaryDirectory(prefix='swr-irq-test-') as tmp:
    work = Path(tmp)
    shutil.copyfile(H / 'tests/swr_irq.c', work / 'test.c')
    for name in ['production'] + (list(mutations) if args.negative else []):
        text = production
        if name != 'production':
            old, new = mutations[name]
            assert text.count(old) == 1, (name, text.count(old))
            text = text.replace(old, new, 1)
        (work / 'swr_irq_production.inc').write_text(text)
        compile_result = subprocess.run([str(clang), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread', 'test.c', '-o', name],
            cwd=work, text=True, capture_output=True)
        assert compile_result.returncode == 0, compile_result.stderr
        try:
            run = subprocess.run([str(work / name)], cwd=work, text=True, capture_output=True, timeout=8)
            accepted = run.returncode == 0
            log = run.stdout + run.stderr
        except subprocess.TimeoutExpired:
            accepted = False
            log = 'Timed out while testing the mutated production synchronization path.'
        assert accepted == (name == 'production'), (name, log)
        results.append({'name': name, 'status': 'PASS' if accepted else 'REJECTED', 'log': log[-6000:]})

sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {
    'status': 'PASS', 'source_sha256': {str(path): sha(path) for path in [provider, controller, header]},
    'fixture_sha256': sha(H / 'tests/swr_irq.c'), 'verifier_sha256': sha(Path(__file__)),
    'public_device_and_master_layout': 'unchanged', 'scenarios': 9,
    'production': results[0], 'negative_controls': results[1:],
    'limits': ['Production synchronization functions and controller pending loop compiled with host service mocks.',
               'No physical SoundWire, regmap IRQ, Linux device removal, or phone test.'],
}
args.output.write_text(json.dumps(report, indent=2) + '\n')
print('PASS: SoundWire IRQ lifecycle, 9 scenarios,', len(results) - 1, 'compiled rejecting controls')
