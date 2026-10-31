#!/usr/bin/env python3
"""Exercise extracted VA-minidump code with sanitizer-backed provider mocks."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = 'drivers/soc/qcom/qcom_va_minidump.c'


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    m = re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b' + name + r'\([^;]*?\)\n\{', source)
    if not m:
        raise ValueError(name)
    end, depth = m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'


def structure(source, name):
    return re.search(r'(?m)^struct ' + name + r' \{.*?^};', source, re.S)[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--source-dir', default='onyx')
    ap.add_argument('--output', type=Path)
    a = ap.parse_args()
    w = a.workspace.resolve()
    root = w / a.source_dir
    src = (root / SOURCE).read_text()
    public = (w / 'onyx/include/soc/qcom/minidump.h').read_text()
    elf = (w / 'onyx/drivers/soc/qcom/elf.h').read_text()
    structs = '\n'.join(structure(public, n) for n in ['md_region', 'va_md_entry'])
    structs += '\n' + '\n'.join(structure(src, n) for n in ['va_md_tree_node', 'va_md_elf_info', 'va_minidump_data', 'notifier_block_list', 'va_md_s_data'])
    structs += '\nstatic struct va_minidump_data va_md_data;\n'
    funcs = ['va_md_add_entry', 'va_md_check_overlap', 'va_md_tree_insert', 'va_md_overflow_check', 'qcom_va_md_add_region', 'qcom_va_md_minidump_registration', 'set_sec_name', 'qcom_va_add_elf_hdr', 'qcom_va_add_hdrs', 'qcom_va_md_calc_size', 'qcom_va_md_calc_elf_size']
    elf_names = ['elf_sheader', 'elf_section', 'elf_pheader', 'elf_program', 'elf_str_table']
    groups = {
        'vamd_elf': '\n'.join(function(elf, n) for n in elf_names) + '\n' + '\n'.join(function(src, n) for n in funcs),
        'vamd_panic': '\n'.join(function(elf, n) for n in elf_names) + '\n' + '\n'.join(function(src, n) for n in funcs + ['qcom_va_md_enabled', 'qcom_va_md_panic_handler', 'qcom_va_md_elf_panic_handler']),
        'vamd_lifetime': '\n'.join(function(src, n) for n in ['va_md_kobj_release', 'qcom_va_md_enabled', 'qcom_va_md_register', 'qcom_va_md_unregister', 'qcom_va_md_reserve_mem', 'qcom_va_md_driver_probe', 'qcom_va_md_driver_remove']),
    }
    results = {}
    with tempfile.TemporaryDirectory(prefix='onyx-vamd-test-') as temp:
        d = Path(temp)
        for name, code in groups.items():
            text = (HERE / 'tests' / (name + '.c')).read_text()
            text = text.replace('/* STRUCTURES */', structs).replace('/* FUNCTIONS */', code)
            for tag, native in [('elfhdr', 'Elf64_Ehdr'), ('elf_shdr', 'Elf64_Shdr'), ('elf_phdr', 'Elf64_Phdr')]:
                text = text.replace('struct ' + tag, native)
            c = d / (name + '.c')
            c.write_text(text)
            exe = d / name
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-sign-compare', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(c), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
            if run.returncode:
                raise RuntimeError(name + ': ' + run.stderr)
            results[name] = json.loads(run.stdout)
    imports = json.loads((HERE / 'vamd-imports.json').read_text())
    assert [e['path'] for e in imports] == [SOURCE]
    for item in imports:
        assert subprocess.check_output(['git', '-C', str(w / item['project']), 'rev-parse', item['revision'] + ':' + item['path']], text=True).strip() == item['blob']
    assert 'dma_free_coherent' not in function(src, 'qcom_va_md_driver_probe').split('unregister_panic:')[0]
    assert 'vunmap' not in src and 'vmap(' not in src
    assert '.release = va_md_kobj_release' in src
    assert '"%s", name' in function(src, 'qcom_va_md_register')
    assert 'list_for_each_entry_rcu' in function(src, 'qcom_va_md_panic_handler')
    assert 'copy_from_kernel_nofault' in function(src, 'qcom_va_add_hdrs')
    shared = json.loads((HERE / 'vamd-shared-headers.json').read_text())
    for path, expected in shared.items():
        assert hashlib.sha256((w / 'onyx' / path).read_bytes()).hexdigest() == expected
    notifier = (w / 'common/kernel/notifier.c').read_text()
    call = function(notifier, 'atomic_notifier_call_chain').split('{', 1)[1]
    assert call.index('rcu_read_lock') < call.index('notifier_call_chain(') < call.index('rcu_read_unlock')
    assert 'synchronize_rcu' in function(notifier, 'atomic_notifier_chain_unregister')
    remove = function(src, 'qcom_va_md_driver_remove')
    assert remove.index('va_md_init, false') < remove.index('atomic_notifier_chain_unregister') < remove.index('kset_unregister') < remove.index('msm_minidump_remove_region') < remove.index('dma_free_coherent')
    add = function(src, 'qcom_va_md_add_region')
    assert 'check_add_overflow' in add and 'capture_cpu' in add and 'collecting' in add
    results['source_sha256'] = {SOURCE: hashlib.sha256(src.encode()).hexdigest()}
    results['shared_headers_sha256'] = {path: hashlib.sha256((w / 'onyx' / path).read_bytes()).hexdigest() for path in ['include/soc/qcom/minidump.h', 'drivers/soc/qcom/elf.h']}
    results['panic_notifier_rcu_contract_sha256'] = hashlib.sha256(notifier.encode()).hexdigest()
    results['harness_sha256'] = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in (HERE / 'tests').glob('vamd_*.c')}
    results['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    results['limits'] = 'Extracted source functions with mock DMA/OF/kobject/notifier/RCU services and decoded ELF checks. No live panic, cache coherency, CMA, firmware, kernel RCU concurrency or phone validation.'
    report = json.dumps(results, indent=2) + '\n'
    if a.output:
        a.output.write_text(report)
    print(report, end='')


if __name__ == '__main__':
    main()
