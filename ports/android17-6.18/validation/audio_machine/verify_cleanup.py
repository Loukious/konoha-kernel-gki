#!/usr/bin/env python3
"""Execute production card cleanup and event-list ownership with rejecting controls."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from verify_smartpa import function
from verify_common import declaration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--provider-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--cc', default=shutil.which('clang') or shutil.which('cc'))
    args = parser.parse_args()
    src = args.source_root / 'qcom/opensource/audio-kernel/asoc/sun.c'
    provider = args.provider_root / 'qcom/opensource/audio-kernel/soc/snd_event.c'
    source = src.read_text()
    fields = declaration(source, 'struct msm_asoc_mach_data {')
    body = '\n\n'.join(function(source, n) for n in [
        'msm_machine_put_node', 'msm_machine_get_node', 'msm_rx_tx_codec_exit',
        'msm_append_dai_links', 'msm_audio_ssr_register', 'msm_asoc_machine_cleanup'])
    body += '\n\n' + function(provider.read_text(), 'snd_event_clients_free')
    fixture = Path(__file__).parent / 'tests/card_cleanup.c'
    template = fixture.read_text().replace('/* PRODUCTION_MACHINE_TYPE */', fields)
    mutations = {
        'early_common_free': ('if (pdata->card_registered) {', 'msm_common_snd_deinit(pdata->common_pdata);\n\tif (pdata->card_registered) {'),
        'missing_ssr_drain': ('if (pdata->ssr_registered) {', 'if (false && pdata->ssr_registered) {'),
        'missing_headset_drain': ('if (pdata->mbhc_component) {', 'if (false && pdata->mbhc_component) {'),
        'stale_calibration_pointer': ('pdata->mbhc_cfg.calibration = NULL;', '(void)pdata->mbhc_cfg.calibration;'),
        'failed_master_list_leak': ('snd_event_clients_free(clients);', '(void)clients;'),
        'failed_node_pointer_retained': ('if (ret)\n\t\t*node = NULL;', 'if (false && ret)\n\t\t*node = NULL;'),
        'dai_capacity_overrun': ('num > capacity - *count', '(false && num > capacity - *count)'),
    }
    report = {'status': 'PASS', 'negative_controls': [], 'source_sha256': {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [src, provider]},
        'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'limits': 'Mocked ASoC unregister, MBHC stop, GPIO, SND event, devres and DT services. Tests selected production cleanup paths; not full probe, real kernel concurrency, DSP or hardware.'}
    cases = [('positive', body)]
    if args.negative:
        for name, (old, new) in mutations.items():
            assert body.count(old) == 1, (name, body.count(old))
            cases.append((name, body.replace(old, new)))
    with tempfile.TemporaryDirectory(prefix='onyx-machine-cleanup-') as tmp:
        for name, code in cases:
            unit = Path(tmp) / (name + '.c'); binary = Path(tmp) / name
            unit.write_text(template.replace('/* PRODUCTION_SOURCE */', code))
            subprocess.run([args.cc, '-std=gnu11', '-g', '-O1', '-Wall', '-Werror',
                            '-Wno-unused-function', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', str(unit), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=15)
            if name == 'positive':
                assert run.returncode == 0, run.stdout + run.stderr
                report['result'] = run.stdout.strip()
            else:
                assert run.returncode != 0, name + ' was not rejected'
                report['negative_controls'].append({'name': name, 'compiled': True, 'rejected': True})
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(report['result']); print('Rejected', len(report['negative_controls']), 'compiled regressions')


if __name__ == '__main__':
    main()
