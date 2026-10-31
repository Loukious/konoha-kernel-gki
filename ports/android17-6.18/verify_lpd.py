#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    m = re.search(r'(?m)^(?:static )?[\w \t*\n]+?\b' + name + r'\([^;]*?\)\n\{', source)
    assert m, name
    e, depth = m.end(), 1
    while depth:
        depth += (source[e] == '{') - (source[e] == '}')
        e += 1
    return source[m.start():e]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    base = args.workspace / args.package / 'drivers/power/supply/mca/mca_hardware_monitor'
    source = (base / 'mca_lpd_detect.c').read_text()
    header = (base / 'inc/mca_lpd_detect.h').read_text()
    table = source[source.index('struct mca_sysfs_attr_info lpd_sysfs_field_tbl[]'):source.index('static int lpd_sysfs_create_group')]
    table = table[:table.index('static ssize_t lpd_sysfs_show')]
    names = ['lpd_is_charging_limit', 'lpd_sysfs_show', 'lpd_sysfs_store', 'mca_lpd_detect_probe', 'mca_lpd_detect_remove', 'mca_lpd_detect_shutdown']
    definitions = '\n'.join(function(source, n) for n in names)
    globals = source[source.index('static struct mca_lpd_dev __rcu'):source.index('int lpd_is_charging_limit')]
    test = (HERE / 'tests/lpd_lifecycle.c').read_text().replace('/* HEADER */', header).replace('/* GLOBALS */', globals).replace('/* TABLE */', table).replace('/* FUNCTIONS */', definitions)
    with tempfile.TemporaryDirectory(prefix='onyx-lpd-tests-') as tmp:
        d = Path(tmp)
        (d / 'test.c').write_text(test)
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(d / 'test.c'), '-o', str(d / 'test')], check=True)
        run = subprocess.run([str(d / 'test')], text=True, capture_output=True, timeout=20)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        report = json.loads(run.stdout)
    report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    report['source_sha256'] = {str(f.relative_to(base)): hashlib.sha256(f.read_bytes()).hexdigest() for f in [base / 'mca_lpd_detect.c', base / 'inc/mca_lpd_detect.h']}
    report['limits'] = 'Extracted production lifecycle/sysfs/getter functions with pthread SRCU/mutex models and mocked hardware/sysfs/devm APIs. No live kernel/sysfs, firmware, moisture or hardware validation.'
    output = json.dumps(report, indent=2) + '\n'
    print(output, end='')
    if args.output:
        args.output.write_text(output)

if __name__ == '__main__':
    main()
