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
parser.add_argument('--source-dir', default='onyx_wcd_codecs')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--negative', action='store_true')
parser.add_argument('--suite', action='append')
args = parser.parse_args()
S = args.workspace / args.source_dir
clang = args.workspace / 'prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang'
assert clang.is_file()

def function(text, name):
    match = re.search(r'(?m)^[^\n;]*\b' + re.escape(name) + r'\([^;]*?\n\{', text)
    assert match, name
    brace = text.index('{', match.start())
    depth = 0
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for item in tokens.finditer(text, brace):
        if item.group() == '{':
            depth += 1
        elif item.group() == '}':
            depth -= 1
            if depth == 0:
                return text[match.start():item.end()]
    raise AssertionError(name)

def normalize(text, variant):
    return text.replace(variant.upper(), 'CODEC').replace(variant, 'codec')

def run_suite(name, variant, source, fixture, mutations):
    results = []
    with tempfile.TemporaryDirectory(prefix='wcd-' + name + '-') as temp:
        work = Path(temp)
        shutil.copyfile(H / 'tests' / fixture, work / 'test.c')
        cases = ['production'] + (list(mutations) if args.negative else [])
        for case in cases:
            text = source
            if case != 'production':
                old, new = mutations[case]
                assert text.count(old) == 1, (name, variant, case, text.count(old))
                text = text.replace(old, new, 1)
            (work / (name + '_production.inc')).write_text(text)
            command = [str(clang), '-std=gnu11', '-O1', '-g', '-Wall', '-Werror', '-pthread',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       '-DCODEC_KIND=' + {'wcd9378': '9378', 'wcd938x': '9380', 'wcd939x': '9390'}[variant],
                       'test.c', '-o', case]
            compile_result = subprocess.run(command, cwd=work, text=True, capture_output=True)
            assert compile_result.returncode == 0, (name, variant, case, compile_result.stderr)
            try:
                run = subprocess.run([str(work / case)], cwd=work, text=True, capture_output=True, timeout=10)
                success = run.returncode == 0
                log = run.stdout + run.stderr
            except subprocess.TimeoutExpired:
                success = False
                log = 'Timed out while exercising the compiled production lifecycle path.'
            assert success == (case == 'production'), (name, variant, case, log)
            results.append({'case': case, 'status': 'PASS' if success else 'REJECTED', 'log': log[-7000:]})
    return {'suite': name, 'variant': variant, 'results': results}

reports = []
source_hashes = {}
for variant in ['wcd9378', 'wcd938x', 'wcd939x']:
    base = Path('qcom/opensource/audio-kernel/asoc/codecs') / variant
    source = (S / base / (variant + '-mbhc.c')).read_text()
    names = ['_mbhc_stop_locked', '_mbhc_hs_detect', '_mbhc_hs_detect_exit', '_mbhc_ssr_down',
             '_mbhc_post_ssr_init_locked', '_mbhc_post_ssr_init', '_mbhc_recover',
             '_mbhc_init_locked', '_mbhc_init', '_mbhc_deinit']
    code = '\n\n'.join(function(source, variant + suffix) for suffix in names)
    code = normalize(code, variant)
    constants = sorted(set(re.findall(r'\bCODEC_[A-Z0-9_]+\b', code)))
    code = 'enum { ' + ', '.join(name + ' = ' + str(i + 1) for i, name in enumerate(constants)) + ' };\n' + code
    mutations = {
        'stale_card_configuration': ('if (release_config)\n\t\tpriv->mbhc_config = NULL;', 'if (release_config)\n\t\t(void)priv;'),
        'stale_core_configuration': ('\t\t\tmbhc->wcd_mbhc.mbhc_cfg = NULL;', '\t\t\t(void)mbhc;'),
        'lost_ssr_configuration': ('wcd_mbhc_start(&priv->mbhc->wcd_mbhc, priv->mbhc_config);',
                                   'wcd_mbhc_start(&priv->mbhc->wcd_mbhc, priv->mbhc->wcd_mbhc.mbhc_cfg);'),
        'missing_mbhc_deinit': ('wcd_mbhc_deinit(&priv->mbhc->wcd_mbhc);', '(void)priv;'),
    }
    permanent_exit = function(code, 'codec_mbhc_hs_detect_exit')
    mutations['missing_lifecycle_serialization'] = (permanent_exit,
        permanent_exit.replace('\tmutex_lock(&priv->mbhc_lock);\n', '').replace('\tmutex_unlock(&priv->mbhc_lock);\n', ''))
    if not args.suite or 'mbhc' in args.suite:
        reports.append(run_suite('mbhc', variant, code, 'mbhc_lifecycle.c', mutations))
    if not args.suite or 'bind' in args.suite:
        main_source = (S / base / (variant + '.c')).read_text()
        array = re.search(r'static const int ' + variant + r'_wd_irqs\[\] = \{.*?\n\};', main_source, re.S).group()
        names = ['_release_bind_resources', '_bind', '_unbind']
        code = normalize(array + '\n' + '\n\n'.join(function(main_source, variant + suffix) for suffix in names), variant)
        constants = sorted(set(re.findall(r'\b(?:CODEC_|SWRS_SCP_SDCA_)[A-Z0-9_]+\b', code)))
        code = 'enum { ' + ', '.join(name + ' = ' + str(i + 1) for i, name in enumerate(constants)) + ' };\n' + code
        mutations = {
            'missing_retained_device_put': ('put_device(&priv->rx_swr_dev->dev);', '(void)priv;'),
            'missing_irq_withdrawal': ('ret = swr_set_slave_irq(priv->tx_swr_dev, NULL);', 'ret = 0;'),
            'missing_watchdog_unwind': ('wcd_free_irq(&priv->irq_info,\n\t\t\t     codec_wd_irqs[--priv->wd_irqs_requested], NULL);', '--priv->wd_irqs_requested;'),
            'missing_regmap_error_check': ('if (IS_ERR(priv->regmap)) {', 'if (!priv->regmap) {'),
        }
        reports.append(run_suite('bind', variant, code, 'bind_lifecycle.c', mutations))
    if not args.suite or 'slave' in args.suite:
        source = (S / base / (variant + '-slave.c')).read_text()
        declarations = '\n'.join(re.findall(r'^#define SWR_SLV_.*$', source, re.M))
        declarations += '\n' + re.search(r'struct ' + variant + r'_slave_priv \{.*?\n\};', source, re.S).group()
        names = ['get_parameters', 'is_swr_slv_reg_readable', variant + '_swrslave_reg_show',
                 'codec_debug_peek_write', 'codec_debug_write', variant + '_swr_probe', variant + '_swr_remove']
        if variant == 'wcd939x':
            declarations += '\nstatic DEFINE_MUTEX(wcd939x_slave_lock);'
            names += ['wcd939x_slave_register_notify', 'wcd939x_swr_reset']
        code = normalize(declarations + '\n' + '\n\n'.join(function(source, name) for name in names), variant)
        peek = function(code, 'codec_debug_peek_write')
        mutations = {
            'discarded_read_error': (peek, peek.replace('if (ret)\n\t\treturn ret;', 'if (ret)\n\t\treturn count;')),
            'discarded_write_error': ('return ret ? ret : count;', '(void)ret;\n\treturn count;'),
            'missing_debugfs_drain': ('debugfs_remove_recursive(codec_slave->debugfs_codec_dent);', '(void)codec_slave;'),
            'oversized_write_value': (' || params[1] > U8_MAX', ''),
        }
        if variant == 'wcd939x':
            reset = function(code, 'codec_swr_reset')
            mutations['missing_reset_serialization'] = (reset, reset.replace('mutex_lock(&codec_slave_lock);', '').replace('mutex_unlock(&codec_slave_lock);', ''))
        reports.append(run_suite('slave', variant, code, 'slave_io.c', mutations))
    if variant == 'wcd939x' and (not args.suite or 'regulator' in args.suite):
        source = (S / base / (variant + '-regulator.c')).read_text()
        code = normalize(re.sub(r'^#include.*\n', '', source, flags=re.M), variant)
        mutations = {
            'missing_registration_unwind': ('err:\n\tdevres_release_group(dev, wcd_init_mb_regulator);', 'err:\n\t(void)dev;'),
            'missing_vote_release_action': ('ret = devm_add_action_or_reset(dev, wcd_reg_release_votes, priv);', 'ret = 0;\n\t(void)wcd_reg_release_votes;'),
            'lost_failed_disable_vote': ('if (!ret)\n\t\tpriv->mb_enabled_count[micb - 1] = false;', 'priv->mb_enabled_count[micb - 1] = false;'),
        }
        reports.append(run_suite('regulator', variant, code, 'regulator_lifecycle.c', mutations))
    for path in sorted((S / base).glob('*')):
        if path.is_file():
            source_hashes[str(path.relative_to(S))] = hashlib.sha256(path.read_bytes()).hexdigest()

report = {'status': 'PASS', 'source_dir': str(S), 'source_sha256': source_hashes,
          'fixtures_sha256': {str(path.relative_to(H)): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in sorted((H / 'tests').glob('*')) if path.is_file()},
          'verifier_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'suites': reports, 'negative_controls': sum(len(item['results']) - 1 for item in reports),
          'limits': ['Selected production functions compiled with host service mocks and ASan/UBSan.',
                     'No physical codec, kernel module load, regmap IRQ, or phone audio test.']}
args.output.write_text(json.dumps(report, indent=2) + '\n')
print('PASS:', len(reports), 'codec suites;', report['negative_controls'], 'compiled rejecting controls')
