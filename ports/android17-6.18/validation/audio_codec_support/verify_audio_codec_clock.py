#!/usr/bin/env python3
"""Execute the codec clock source with mocked CCF, devres, PRM and pinctrl."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--cc', default=shutil.which('clang') or shutil.which('cc'))
    args = parser.parse_args()
    rel = 'qcom/opensource/audio-kernel/asoc/codecs/audio-ext-clk-up.c'
    path = args.source_root / rel
    source = path.read_text()
    body = source[source.index('\nenum {'):source.index('\nstatic const struct of_device_id audio_ref_clk_match[]')]
    fixture = Path(__file__).resolve().parent / 'tests/audio_codec_clock.c'
    template = fixture.read_text()
    compiler = args.cc
    if not compiler:
        raise SystemExit('A C compiler is required')
    mutations = {
        'discarded_init_pointer': ('clk_priv->clk_init->parent_names', 'hw->init->parent_names'),
        'lost_failed_release': ('else\n\t\t\tclk_priv->enable = 0;', '\n\t\tclk_priv->enable = 0;'),
        'missing_prepare_rollback': ('if (acquired) {', 'if (false && acquired) {'),
        'lost_probe_deferral': ('return PTR_ERR(pnctrl_info->pinctrl);', 'return -EINVAL;'),
        'missing_final_release': ('ret = devm_add_action_or_reset(&pdev->dev, audio_ref_clk_cleanup, clk_priv);', 'ret = 0;'),
    }
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = {
        'status': 'PASS', 'source_sha256': {rel: sha(path)},
        'harness_sha256': {fixture.name: sha(fixture)},
        'verifier_sha256': sha(Path(__file__)),
        'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
        'limits': 'Mocked kernel services; no actual CCF/devres concurrency, PRM firmware, electrical clock, or phone validation.',
        'negative_controls': [],
    }
    with tempfile.TemporaryDirectory(prefix='audio-codec-clock-') as td:
        tmp = Path(td)
        cases = [('positive', body)]
        if args.negative:
            for name, (old, new) in mutations.items():
                assert body.count(old) == 1, name
                cases.append((name, body.replace(old, new, 1)))
        for name, text in cases:
            unit = tmp / (name + '.c')
            binary = tmp / name
            unit.write_text(template.replace('/* PRODUCTION_SOURCE */', text))
            subprocess.run([compiler, '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Wall', '-Werror', '-Wno-unused-parameter',
                '-Wno-unused-function', str(unit), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            if name == 'positive':
                if run.returncode:
                    raise SystemExit(run.stdout + run.stderr)
                report['result'] = run.stdout.strip()
            else:
                assert run.returncode != 0, f'{name}: deliberate regression was not rejected'
                report['negative_controls'].append({'name': name, 'compiled': True, 'rejected': True})
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(report['result'])
    print(f"Rejected {len(report['negative_controls'])} compiled regressions")


if __name__ == '__main__':
    main()
