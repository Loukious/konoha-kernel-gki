import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
p.add_argument('--source-dir', default='onyx')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = a.workspace / a.source_dir
prefix = 'qcom/opensource/video-driver/driver/vidc/src/'
selection = {'venus_hfi.c': ['__schedule_power_collapse_work'], 'msm_vidc_driver.c': ['msm_vidc_schedule_core_deinit'], 'msm_vidc_probe.c': ['msm_vidc_component_master_bind', 'msm_vidc_component_master_unbind']}

def extract(body, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', body)
    assert m, name
    pos = body.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (body[pos] == '{') - (body[pos] == '}')
        pos += 1
    return body[m.start():pos] + '\n'

source = '\n'.join(extract((root / (prefix + file)).read_text(), name) for file, names in selection.items() for name in names)
controls = {
    'missing_irq_drain': ('synchronize_irq(core->irq);', '(void)0;'),
    'missing_pm_drain': ('cancel_delayed_work_sync(&core->pm_work);', '(void)0;'),
    'missing_ssr_drain': ('cancel_work_sync(&core->ssr_work);', '(void)0;'),
    'missing_firmware_work_drain': ('cancel_delayed_work_sync(&core->fw_unload_work);', '(void)0;'),
    'missing_open_gate': ('WRITE_ONCE(core->ready, false);', '(void)0;'),
    'missing_stopping_gate': ('WRITE_ONCE(core->stopping, true);', '(void)0;'),
    'missing_media_drain': ('msm_vidc_deinitialize_media(core);', '(void)0;'),
    'missing_component_release': ('component_unbind_all(dev, core);', '(void)0;'),
    'missing_debugfs_drain': ('debugfs_remove_recursive(core->debugfs_root);', '(void)0;'),
    'missing_scheduler_stop': ('if (READ_ONCE(core->stopping))', 'if (false)'),
}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {prefix + x: sha(root / (prefix + x)) for x in selection}, 'harness_sha256': {'video_teardown.c': sha(H / 'tests/video_teardown.c')}}
with tempfile.TemporaryDirectory(prefix='video-teardown-test-') as tmp:
    for name in ['positive', *controls]:
        current = source
        if name in controls:
            before, after = controls[name]
            assert before in current, name
            current = current.replace(before, after)
        f = Path(tmp) / 'teardown.c'
        f.write_text((H / 'tests/video_teardown.c').read_text().replace('/* SOURCE */', current))
        binary = f.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(f), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['core_lifecycle'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Actual master bind/unbind and scheduling gates with mocked component, firmware, debugfs, media, workqueue and IRQ services; pthread in-flight IRQ model checks ordering before queue release. Hardware power-off and Linux IRQ/workqueue implementation are not executed.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
