#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SMEM = 'qcom/opensource/data-kernel/drivers/smem-mailbox/smem-mailbox.c'
UTILS = 'platform/cnss_utils/cnss_utils.c'

def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.workspace / args.source_dir
    utils, smem = (root / UTILS).read_text(), (root / SMEM).read_text()
    smem_body = re.sub(r'^#include[^\n]*\n', '', smem, flags=re.M)
    smem_body = smem_body[:smem_body.index('static const struct of_device_id')]
    names = {
        'wlan_mac': ['set_wlan_mac_address', 'get_wlan_mac_address',
                     'cnss_utils_smem_mailbox_write', 'cnss_utils_mac_write'],
        'wlan_utils_notifier': ['cnss_utils_status_update_user',
                                'cnss_utils_register_status_notifier',
                                'cnss_utils_unregister_status_notifier'],
    }
    templates = {name: (HERE / 'tests' / (name + '.c')).read_text() for name in names}
    templates['wlan_smem'] = (HERE / 'tests/wlan_smem.c').read_text()
    bodies = {name: '\n'.join(function(utils, fn) for fn in funcs) for name, funcs in names.items()}
    bodies['wlan_smem'] = smem_body
    mutations = {
        'short_descriptor': ('if (!smem || size < sizeof(info))', 'if (!smem)'),
        'full_ring_ambiguity': ('- ring_used(head, tail, c->info.write_size) - 4;',
                                '- ring_used(head, tail, c->info.write_size);'),
        'callback_no_drain': ('synchronize_srcu(&mailbox_callbacks);', '(void)0;'),
    }
    groups = list(mutations) if args.negative else list(bodies)
    tests = {}
    with tempfile.TemporaryDirectory(prefix='onyx-wlan-utils-tests-') as tmp:
        for group in groups:
            name = 'wlan_smem' if args.negative else group
            body = bodies[name]
            if args.negative:
                before, after = mutations[group]
                assert before in body
                body = body.replace(before, after)
            template = templates[name]
            code = template.replace('/* SOURCE */', body) if name == 'wlan_smem' else template.replace('/* FUNCTIONS */', body)
            if name == 'wlan_smem':
                code = '#define EPROBE_DEFER 517\n' + code
            source = Path(tmp) / (group + '.c')
            binary = source.with_suffix('')
            source.write_text(code)
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-no-pie', '-Wall', '-Wextra',
                            '-Wno-unused-parameter', '-Werror=implicit-function-declaration',
                            str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
            if args.negative:
                assert result.returncode, group
                tests[group] = {'rejected': True, 'returncode': result.returncode,
                                'diagnostic': next((line for line in result.stderr.splitlines()
                                                   if 'Assertion' in line or 'runtime error:' in line or 'ERROR: AddressSanitizer' in line), result.stderr[:200])}
            else:
                assert not result.returncode, group + ': ' + result.stdout + result.stderr
                tests[group] = json.loads(result.stdout)
    fixture = HERE / ('wlan-utils-imports.json' if (HERE / 'wlan-utils-imports.json').exists() else 'imports.json')
    pins = json.loads(fixture.read_text())
    assert len(pins) == 10
    generated = [pin for pin in pins if pin['path'].endswith(('_v01.c', '_v01.h'))]
    for pin in generated:
        assert (root / pin['path']).read_bytes() == subprocess.check_output(
            ['git', '-C', str(args.workspace / pin['project']), 'cat-file', 'blob', pin['blob']])
    original = next(pin for pin in pins if pin['path'] == SMEM)
    native = subprocess.check_output(['git', '-C', str(args.workspace / original['project']),
                                      'cat-file', 'blob', original['blob']], text=True)
    for struct in ['client_info', 'smem_info', 'tlv']:
        pattern = r'struct ' + struct + r'\s*\{.*?\};'
        current_layout = re.search(pattern, smem, re.S).group().replace('IRQ_NUM_NAMES', '4')
        original_layout = re.search(pattern, native, re.S).group()
        assert re.sub(r'\s+', '', current_layout) == re.sub(r'\s+', '', original_layout)
    report = {
        'tests': tests,
        'source_sha256': {pin['path']: hashlib.sha256((root / pin['path']).read_bytes()).hexdigest() for pin in pins},
        'harness_sha256': {name + '.c': hashlib.sha256((HERE / 'tests' / (name + '.c')).read_bytes()).hexdigest() for name in templates},
        'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
        'native_descriptor_and_TLV_layouts_unchanged': True,
        'generated_QMI_service_schemas_unchanged': len(generated),
        'limits': 'Production utility functions and full SMEM provider with mocked MMIO/SMEM/IRQ/state/devm APIs and pthread locking/SRCU/IRQ drain. Mutation controls test guards, ring reserve and callback drain. No native modem frames, live MMIO ordering, IRQ/kernel module behavior, firmware, WLAN or phone validation.',
    }
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
