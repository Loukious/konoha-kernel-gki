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
ap.add_argument('workspace', type=Path)
ap.add_argument('--source-root', type=Path, default=H / 'private-source')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
source_name = 'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-rx-macro.c'
p = a.source_root / source_name
s = p.read_text()
headers = ['qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-registers.h',
           'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc.h',
           'qcom/opensource/audio-kernel/include/bindings/qcom,lpass-cdc-clk-rsc.h']

def function(name):
    match = re.search(r'^(?:static )?(?:int|void|bool) ' + name + r'\([^;{]*\{', s, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[match.start():end] + '\n'

def structure(name):
    return re.search(r'^struct ' + name + r' \{.*?^\};', s, re.M | re.S)[0] + '\n'

defines = []
continued = False
for line in s.splitlines():
    if line.startswith('#define ') or continued:
        defines.append(line)
        continued = line.endswith('\\')
layout = '\n'.join(defines) + '\n'
layout += '\n'.join(re.findall(r'^enum(?: \w+)? \{.*?^\};', s, re.M | re.S)) + '\n'
layout += '\n'.join(re.findall(r'^enum(?: \w+)? \{.*?^\};',
    (a.workspace / 'onyx' / headers[1]).read_text(), re.M | re.S)) + '\n'
layout += (a.workspace / 'onyx' / headers[0]).read_text() + '\n'
layout += (a.workspace / 'onyx' / headers[2]).read_text() + '\n'
layout += ''.join(structure(n) for n in ['rx_swr_ctrl_data', 'rx_swr_ctrl_platform_data',
             'lpass_cdc_rx_macro_idle_detect_config', 'lpass_cdc_rx_macro_priv',
             'lpass_cdc_rx_macro_reg_mask_val', 'lpass_cdc_rx_macro_fir_filter_ctl'])
code = ''.join(function(n) for n in ['lpass_cdc_rx_macro_get_data',
            'lpass_cdc_rx_macro_core_vote', 'lpass_cdc_rx_macro_mclk_enable',
            'lpass_cdc_rx_macro_mclk_event', 'rx_swrm_clock',
            'lpass_cdc_rx_macro_fir_set_enabled', 'lpass_cdc_rx_macro_fir_filter_enable_put',
            'lpass_cdc_rx_macro_write_fir_pair', 'set_fir_filter_coeff',
            'lpass_cdc_rx_macro_fir_audio_mixer_get', 'lpass_cdc_rx_macro_fir_audio_mixer_put',
            'lpass_cdc_rx_macro_fir_coeff_num_put', 'lpass_cdc_rx_set_fir_capability',
            'lpass_cdc_rx_macro_reset_clock', 'lpass_cdc_rx_macro_component_cleanup',
            'lpass_cdc_rx_macro_init', 'lpass_cdc_rx_macro_deinit',
            'lpass_cdc_rx_macro_release_clocks', 'lpass_cdc_rx_macro_disable_pm'])
layout += re.search(r'static const struct lpass_cdc_rx_macro_reg_mask_val\s+lpass_cdc_rx_macro_reg_init\[\] = \{.*?^\};', s, re.M | re.S)[0] + '\n'
controls = [
    ('component_publication_too_late', 'rx_priv->component = component;\n\tret = snd_soc_dapm_new_controls', 'rx_priv->component = NULL;\n\tret = snd_soc_dapm_new_controls'),
    ('initialization_error_not_unwound', 'if (lpass_cdc_rx_macro_component_cleanup(rx_priv))', 'if (false)'),
    ('FIR_vote_not_cleaned', 'ret = lpass_cdc_rx_macro_fir_set_enabled(rx_priv->component, rx_priv, false);', 'ret = 0;'),
    ('DAPM_vote_not_cleaned', 'if (rx_priv->dapm_mclk_enable) {', 'if (false) {'),
    ('SSR_failed_release_owner_lost', 'rx_priv->ssr_clock_owned = true;', 'rx_priv->ssr_clock_owned = false;'),
    ('final_PM_state_faked', 'if (!ret)\n\t\t\tpm_runtime_set_suspended', 'if (ret <= 0)\n\t\t\tpm_runtime_set_suspended'),
]
rejections = []
with tempfile.TemporaryDirectory(prefix='rx-cleanup-') as td:
    d = Path(td)
    (d / 'rx_resource_layout.inc').write_text(layout)
    fragment = d / 'rx_resource_functions.inc'
    fragment.write_text(code)
    command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-misleading-indentation',
               '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
               str(H / 'tests/audio_rx_cleanup.c'), '-o', str(d / 'resources')]
    def run():
        build = subprocess.run(command, text=True, capture_output=True)
        assert build.returncode == 0, build.stderr
        return subprocess.run([str(d / 'resources')], text=True, capture_output=True, timeout=30,
            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
    result = run()
    assert not result.returncode, result.stdout + result.stderr
    tests = json.loads(result.stdout)
    print(result.stdout.strip(), flush=True)
    if a.negative:
        for name, old, new in controls:
            assert old in code, name
            fragment.write_text(code.replace(old, new, 1))
            result = run()
            assert result.returncode, name
            rejections.append({'name': name, 'rejected': True, 'diagnostic': result.stderr[:500]})
            print('REJECTED', name, flush=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report = {'status': 'PASS', 'tests': tests, 'negative_controls': rejections,
          'source_sha256': {source_name: sha(p)},
          'header_sha256': {n: sha(a.workspace / 'onyx' / n) for n in headers},
          'harness_sha256': {n: sha(H / 'tests' / n) for n in ['audio_rx_resources.c', 'audio_rx_cleanup_services.h', 'audio_rx_cleanup.c']},
          'verifier_sha256': {Path(__file__).name: sha(Path(__file__))},
          'limits': 'Actual component init/cleanup, clock/SSR teardown and final PM functions and private layout; '
                    'real source/provider constants, mocked PM/clock/pinctrl/regmap/ALSA services. '
                    'FIR RAM/pair writes are modeled and not hardware proof. Register failures may follow writes. FS has a void '
                    'interface. ALSA init invokes modeled power callbacks; route tables are not executed. No hardware, kernel PM or full ALSA/SSR validation.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
