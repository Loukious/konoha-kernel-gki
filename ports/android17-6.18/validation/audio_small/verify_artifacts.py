#!/usr/bin/env python3
"""Check native module units, feature tables, metadata and exact provider CRCs."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

H = Path(__file__).resolve().parent
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
PREFIX = 'qcom/opensource/audio-kernel/'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031'))
    parser.add_argument('--package', default='onyx_audio_small')
    parser.add_argument('--module', action='append')
    parser.add_argument('--output', type=Path, default=H / 'module-verification.json')
    parser.add_argument('--capture', action='store_true')
    args = parser.parse_args()
    W = args.workspace
    D, B = W / args.package, W / 'bazel-bin' / args.package
    names = args.module or list(json.loads((H / 'summary.json').read_text()))
    providers = {
        'pinctrl_lpi_dlkm': [('onyx', n) for n in ('spf_core_dlkm', 'q6_notifier_dlkm', 'snd_event_dlkm')],
        'sdca_registers_dlkm': [],
        'swr_dmic_dlkm': [('onyx_swr_irq', 'swr_dlkm'), ('onyx_wcd_codecs', 'wcd939x_dlkm')],
        'swr_haptics_dlkm': [('onyx_swr_irq', 'swr_dlkm'), ('onyx', 'rproc_qcom_common')],
        'stub_dlkm': [],
        'hdmi_dlkm': [('onyx_extdisp_audio', 'msm_ext_display')],
    }
    sys.path.insert(0, str(P))
    from verify_modules import verify
    common_symvers = W / 'bazel-bin/common/kernel_aarch64/Module.symvers'
    release = (W / 'bazel-bin/common/kernel_aarch64/include/config/kernel.release').read_text().strip()
    report = {'kernel_release': release, 'modules': {}, 'providers': {}, 'native_tables': {},
              'source_sha256': {}, 'configuration': {}, 'native_selection': {}}
    report['providers']['common:kernel_aarch64'] = {'path': str(common_symvers), 'sha256': sha(common_symvers)}
    pins = json.loads((H / 'imports.json').read_text())
    paths = json.loads((H / 'sources.json').read_text())
    for pin in pins:
        path = pin['path']
        blob = subprocess.check_output(['git', '-C', str(W / pin['project']), 'rev-parse',
                                         pin['revision'] + ':' + path], text=True).strip()
        assert blob == pin['blob']
        original = subprocess.check_output(['git', '-C', str(W / pin['project']), 'cat-file', 'blob', blob])
        assert original == (H / 'original' / path).read_bytes()
        assert (D / path).read_bytes() == (H / 'private-source' / path).read_bytes()
        report['source_sha256'][path] = sha(D / path)
        if path.endswith('.c'):
            before, after = original.decode(), (D / path).read_text()
            pattern = r'(?m)^(?:(?:static|const)\s+)*(?:struct\s+\w+|char|int|u\d+)\b[^\n;{}]*\[\][^\n;{}]*=\s*\{'
            for table in re.finditer(pattern, before):
                end = before.index('\n};', table.end()) + 3
                text = before[table.start():end]
                assert text in after, (path, text[:100])
                report['native_tables'][path + ':' + table.group().strip()] = hashlib.sha256(text.encode()).hexdigest()
            # All driver-defined constants remain native except the corrected debugfs line width.
            constants = re.findall(r'(?m)^#define[^\n]*(?:\\\n[^\n]*)*', before)
            for constant in constants:
                if constant == '#define BYTES_PER_LINE (20)':
                    assert '#define BYTES_PER_LINE (17)' in after
                else:
                    assert constant in after, (path, constant)

    config_path = PREFIX + 'config/sunautoconf.h'
    native_config = W / 'baseline/kernel/xiaomi/sm8735-modules' / config_path
    assert (W / 'onyx' / config_path).read_bytes() == native_config.read_bytes()
    required_defines = ('CONFIG_PINCTRL_LPI', 'CONFIG_SND_SOC_SDCA_REGISTERS',
        'CONFIG_SND_SOC_SWR_DMIC', 'CONFIG_SND_SWR_HAPTICS', 'CONFIG_SND_SOC_MSM_STUB',
        'CONFIG_SND_SOC_MSM_HDMI_CODEC_RX', 'CONFIG_SND_SOC_AW882XX', 'CONFIG_SND_SOC_WCD939X')
    for symbol in required_defines:
        assert '#define ' + symbol + ' 1' in native_config.read_text().splitlines()
    report['native_selection'] = {'sunautoconf_sha256': sha(native_config), 'enabled': required_defines,
        'stub': 'Native AW882XX branch: 8 channels, SNDRV_PCM_RATE_8000_96000',
        'dmic': 'Native WCD939x micbias/notifier provider branch'}

    for name in names:
        native = json.loads((H / (name + '.json')).read_text())
        assert sha(Path(native['module'])) == native['module_sha256']
        root = B / name
        module = root / 'unstripped' / (name + '.ko')
        assert module.exists(), module
        symvers = root / (name + '_Module.symvers')
        provider_paths = [common_symvers]
        for package, target in providers[name]:
            if args.package == 'onyx':
                package = 'onyx'
            path = W / 'bazel-bin' / package / target / (target + '_Module.symvers')
            provider_paths.append(path)
            report['providers'][package + ':' + target] = {'path': str(path), 'sha256': sha(path)}
        record = verify([module], provider_paths, release)[0]
        assert sorted(record['depends'].split(',')) == sorted(native['depends'].split(',')), name
        assert sorted(record['aliases']) == sorted(native['aliases']), name
        symbols = subprocess.check_output(['readelf', '-sW', str(module)], text=True)
        exports = sorted(m.group(1) for m in re.finditer(r'^.*?\b(?:OBJECT|NOTYPE)\b.*?\b__ksymtab_(\w+)\s*$', symbols, re.M))
        assert exports == native['exports'], (name, exports)
        params = subprocess.check_output(['modinfo', '-F', 'parm', str(module)], text=True).splitlines()
        native_params = subprocess.check_output(['modinfo', '-F', 'parm', native['module']], text=True).splitlines()
        assert params == native_params == []
        for field in ('name', 'license'):
            expected = subprocess.check_output(['modinfo', '-F', field, native['module']], text=True)
            assert subprocess.check_output(['modinfo', '-F', field, str(module)], text=True) == expected
        make_root = B / (name + '_makefiles')
        makefiles = make_root / 'makefiles'
        kbuild = (makefiles / 'Kbuild').read_text()
        units = [entry['path'] for entry in native['objects']]
        assert re.findall(r'^' + name + r'-y \+= (.*)$', kbuild, re.M) == [p[:-2] + '.o' for p in units]
        assert len(units) == 1
        flags = (makefiles / (name + '.cflags_shipped')).read_text()
        assert '-include\n$(ROOT_DIR)/onyx/' + config_path in flags
        copts = json.loads((make_root / 'copts.json').read_text())
        assert [v['expanded'] for v in copts] == ['-include', 'onyx/' + config_path]
        assert not json.loads((make_root / 'removed_copts.json').read_text())
        config = B / (name + '_config/out_dir/.config')
        for item in ('CONFIG_MODULES=y', 'CONFIG_MODVERSIONS=y', 'CONFIG_OF=y',
                     'CONFIG_PM=y', 'CONFIG_COMMON_CLK=y', 'CONFIG_SND_SOC=y'):
            assert item in config.read_text().splitlines(), (name, item)
        record.update(exports=exports, params=params, native_units=units,
                      module_path=str(module), module_symvers_sha256=sha(symvers),
                      kbuild_sha256=sha(makefiles / 'Kbuild'), cflags_sha256=sha(makefiles / (name + '.cflags_shipped')))
        report['modules'][name] = record
        report['configuration'][name] = {'sha256': sha(config), 'path': str(config)}
        if args.capture:
            dest = H / 'artifacts' / name
            dest.mkdir(parents=True, exist_ok=True)
            for src in (module, symvers, config):
                shutil.copyfile(src, dest / (src.name if src.name != '.config' else 'module.config'))
            shutil.copytree(make_root, dest / 'build-inputs', dirs_exist_ok=True)
    if args.capture:
        dest = H / 'artifacts/providers'
        dest.mkdir(parents=True, exist_ok=True)
        for key, data in report['providers'].items():
            shutil.copyfile(data['path'], dest / (key.replace(':', '_') + '_Module.symvers'))
        shutil.copyfile(native_config, H / 'artifacts/sunautoconf.h')
    report['imports_checked'] = sum(item['imports_checked'] for item in report['modules'].values())
    report['limits'] = 'Build/metadata/CRC/native-selection checks only; no module-load, ASoC/DSP, physical audio or boot validation.'
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(names)} native modules, {report["imports_checked"]} imported CRCs, '
          f'{len(report["native_tables"])} unchanged native tables.')


if __name__ == '__main__':
    main()
