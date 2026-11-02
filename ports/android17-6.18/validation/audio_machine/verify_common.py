#!/usr/bin/env python3
"""Run production common-audio lifecycle helpers with fault-injected services."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from verify_smartpa import function


def declaration(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n};', start) + 3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--cc', default=shutil.which('clang') or shutil.which('cc'))
    args = parser.parse_args()
    root = args.source_root / 'qcom/opensource/audio-kernel/asoc'
    source = (root / 'msm_common.c').read_text()
    common_type = declaration((root / 'msm_common.h').read_text(), 'struct msm_common_pdata {')
    parts = []
    for name in ['aud_dev_sysfs_store', 'aud_dev_sysfs_ops', 'aud_dev_kobj_release',
                 'aud_dev_ktype', 'aud_dev_sysfs_init', 'snd_card_notify_user',
                 'snd_card_set_card_status', 'snd_card_sysfs_show', 'snd_card_sysfs_store',
                 'snd_card_sysfs_ops', 'snd_card_kobj_release', 'snd_card_ktype',
                 'snd_card_sysfs_init', 'snd_card_sysfs_deinit', 'mi2s_tdm_hw_vote_req',
                 'msm_common_snd_hw_params', 'msm_common_snd_shutdown',
                 'msm_audio_add_qos_request', 'msm_audio_remove_qos_request',
                 'msm_audio_update_qos_request', 'msm_common_snd_init',
                 'msm_common_snd_deinit', 'msm_common_snd_card_reset']:
        if name.endswith('_ops'):
            parts.append(declaration(source, 'static const struct sysfs_ops ' + name))
        elif name.endswith('_ktype'):
            parts.append(declaration(source, 'static struct kobj_type ' + name))
        else:
            parts.append(function(source, name))
    body = '\n\n'.join(parts)
    fixture = Path(__file__).parent / 'tests/common_lifetime.c'
    template = fixture.read_text().replace('/* PRODUCTION_COMMON_TYPE */', common_type)
    mutations = {
        'ignored_sysfs_parse': ('sscanf(buf, "%u %u %c", &pcm_id, &state, &extra) != 2', 'sscanf(buf, "%u %u %c", &pcm_id, &state, &extra) < 0'),
        'sparse_cpu_allocation': ('kcalloc(nr_cpu_ids, sizeof(*msm_audio_req), GFP_KERNEL)', 'kcalloc(2, sizeof(*msm_audio_req), GFP_KERNEL)'),
        'remove_inactive_qos': ('if (dev_pm_qos_request_active(&msm_audio_req[cpu]))', 'if (true)'),
        'missing_node_release': ('of_node_put(pdata->mi2s_gpio_p[i]);', '(void)pdata->mi2s_gpio_p[i];'),
        'lost_clock_deferral': ('if (ret == -EPROBE_DEFER)\n\t\t\tgoto fail;', 'if (false && ret == -EPROBE_DEFER)\n\t\t\tgoto fail;'),
        'missing_hw_vote_cleanup': ('if (atomic_read(&pdata->lpass_audio_hw_vote_ref_cnt) > 0) {\n\t\tdigital_cdc', 'if (false && atomic_read(&pdata->lpass_audio_hw_vote_ref_cnt) > 0) {\n\t\tdigital_cdc'),
        'failed_prm_vote_leak': ('if (ret < 0 && hw_vote_acquired)', 'if (false && ret < 0 && hw_vote_acquired)'),
    }
    report = {'status': 'PASS', 'negative_controls': [], 'source_sha256': {
        str(root / name): hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ['msm_common.c', 'msm_common.h']},
        'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'limits': 'Mocked sysfs/kobject, CPU topology, QoS, clock and DT services. No real kernel lifetime, firmware, concurrency or audio validation.'}
    cases = [('positive', body)]
    if args.negative:
        for name, (old, new) in mutations.items():
            assert body.count(old) == 1, (name, body.count(old))
            cases.append((name, body.replace(old, new)))
    with tempfile.TemporaryDirectory(prefix='onyx-machine-common-') as tmp:
        for name, code in cases:
            unit = Path(tmp) / (name + '.c'); binary = Path(tmp) / name
            unit.write_text(template.replace('/* PRODUCTION_SOURCE */', code))
            subprocess.run([args.cc, '-std=gnu11', '-g', '-O1', '-Wall', '-Werror',
                            '-Wno-unused-function', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-pthread', str(unit), '-o', str(binary)], check=True)
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
