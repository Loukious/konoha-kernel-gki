#!/usr/bin/env python3
"""Compile production WSA setup/teardown and exercise every injectable failure."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import resource
import subprocess
import tempfile

H = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=H / 'private-source')
parser.add_argument('--output', type=Path, default=H / 'probe-validation.json')
parser.add_argument('--negative', action='store_true')
args = parser.parse_args()


def definition(text, signature):
    start = text.index(signature)
    begin = text.index('{', start)
    level = 1
    i = begin + 1
    # These extracted functions have no literal braces in strings/comments.
    while level:
        level += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i]


results = {}
controls = []
fixture = (H / 'tests/wsa_probe.c.in').read_text()
for codec in ['wsa883x', 'wsa884x']:
    path = args.source_root / 'qcom/opensource/audio-kernel/asoc/codecs' / codec / (codec + '.c')
    text = path.read_text()
    functions = [codec + '_gpio_ctrl', codec + '_enable_supplies',
                 'regdump_read', codec + '_swr_cleanup', codec + '_swr_probe',
                 codec + '_swr_remove']
    if codec == 'wsa884x':
        functions.insert(2, codec + '_parse_port_params')
    chunks = [definition(text, 'static int ' + fn + '(') for fn in functions]
    enum = re.search(r'enum \{\s*' + codec.upper() + r'_IRQ_INT_SAF2WAR[\s\S]*?\n\};', text).group()
    compiled = fixture.replace('@@IRQ_ENUM@@', enum).replace('@@PRODUCTION@@', '\n\n'.join(chunks))
    compiled = compiled.replace(codec, 'codec').replace(codec.upper(), 'CODEC')
    with tempfile.TemporaryDirectory(prefix='onyx-wsa-probe-') as tmp:
        generated = Path(tmp) / 'probe.c'
        generated.write_text(compiled)
        binary = Path(tmp) / 'probe'
        cmd = ['gcc', '-std=gnu11', '-g', '-O1', '-Wall', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-fno-sanitize-recover=all', '-fno-pie', '-no-pie',
               '-DIS884=' + str(int(codec == 'wsa884x')), str(generated), '-o', str(binary)]
        proc = subprocess.run(cmd, text=True, capture_output=True)
        if proc.returncode:
            debug = H / ('failed-' + codec + '-probe.c')
            debug.write_text(compiled)
            raise SystemExit(proc.stdout + proc.stderr + '\nGenerated source: ' + str(debug))
        proc = subprocess.run([str(binary)], text=True, capture_output=True)
        if proc.returncode:
            debug = H / ('failed-' + codec + '-probe.c')
            debug.write_text(compiled)
            raise SystemExit(proc.stdout + proc.stderr + '\nGenerated source: ' + str(debug))
        results[codec] = {'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          'functions': functions, 'result': proc.stdout.strip(),
                          'sanitizers': ['address', 'undefined'],
                          'compiled_fixture_sha256': hashlib.sha256(compiled.encode()).hexdigest()}
        print(proc.stdout, end='')
        if args.negative:
            mutations = [
                ('irq_failure_ignored',
                 '\tif (ret)\n\t\tgoto err;\n\tset_bit(CODEC_IRQ_INT_SAF2WAR,',
                 '\tset_bit(CODEC_IRQ_INT_SAF2WAR,'),
                ('irq_cookie_lost',
                 'wcd_free_irq(&codec->irq_info, irq, codec);',
                 'wcd_free_irq(&codec->irq_info, irq, NULL);'),
                ('irq_withdrawal_lost',
                 'ret = swr_set_slave_irq(codec->swr_slave, NULL);',
                 'ret = 0;'),
                ('lock_initialization_lost',
                 'mutex_init(&codec->res_lock);',
                 '(void)codec;'),
                ('partial_supply_cleanup_lost',
                 'priv->supplies_initialized = true;',
                 'priv->supplies_initialized = false;'),
                ('register_buffer_bound_lost',
                 'i <= end_reg && count - pos >= line_len;',
                 'i <= end_reg;'),
            ]
            if codec == 'wsa884x':
                mutations += [
                    ('gain_length_check_lost', 'sys_gain_size != sizeof(codec->sys_gains)', 'false'),
                    ('gain_wide_value_check_lost', 'codec->sys_gains[i] >= G_MAX_DB', 'false'),
                ]
            for name, old, new in mutations:
                assert compiled.count(old) == 1, (codec, name, compiled.count(old))
                changed = compiled.replace(old, new)
                # Keep the original fixture declaration used when omitting a call.
                if name == 'lock_initialization_lost':
                    changed = changed.replace('static void mutex_init(', 'void mutex_init(')
                generated.write_text(changed)
                built = subprocess.run(cmd, text=True, capture_output=True)
                assert not built.returncode, (name, built.stderr)
                run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=15,
                    preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert run.returncode, (codec, name, 'control did not fail')
                controls.append({'codec': codec, 'mutation': name, 'rejected': True,
                                 'returncode': run.returncode})
                print('REJECTED', codec, name)
args.output.write_text(json.dumps({'status': 'PASS', 'drivers': results, 'negative_controls': controls,
    'fixture_sha256': hashlib.sha256((H / 'tests/wsa_probe.c.in').read_bytes()).hexdigest(),
    'verifier_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'limitations':
    'Host fixtures exercise production source with mocked Linux APIs; no hardware, IRQ-controller, or live-kernel validation.'}, indent=2) + '\n')
