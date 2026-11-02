from pathlib import Path
import difflib
import hashlib
import json
import shutil

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
S = W / 'onyx_audio_amplifiers'
assert not (H / 'frozen-source').exists(), 'Amplifier handoff already frozen'
paths = json.loads((H / 'sources.json').read_text())
for path in paths:
    dest = S / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(H / 'private-source' / path, dest)
providers = {
    'qcom/opensource/audio-kernel/config/sunautoconf.h': 'onyx',
    'qcom/opensource/audio-kernel/asoc/msm_common.h': 'onyx_audio_machine',
    'include/miev/mievent.h': 'onyx',
}
for path, package in providers.items():
    dest = S / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(W / package / path, dest)
(H / 'provider-inputs.json').write_text(json.dumps({path: {
    'package': package, 'sha256': hashlib.sha256((W / package / path).read_bytes()).hexdigest()
} for path, package in providers.items()}, indent=2) + '\n')
headers = [path for path in paths if path.endswith('.h')] + list(providers)
fragment = 'ddk_headers(\n    name = "audio_amplifier_headers",\n    hdrs = ' + json.dumps(headers) + ',\n'
fragment += '    includes = ["qcom/opensource/audio-kernel/asoc"],\n    linux_includes = ["include"],\n    visibility = ["//visibility:public"],\n)\n\n'
for name, units in json.loads((H / 'units.json').read_text()).items():
    deps = [':audio_amplifier_headers']
    if name != 'sia91xx_tuning_dlkm':
        deps.append('//onyx_audio_machine:machine_dlkm')
    if name == 'aw882xx_dlkm':
        deps.append('//onyx:miev')
    copts = ['-DDEBUG'] if name == 'aw882xx_dlkm' else []
    copts += ['-include', '$(location qcom/opensource/audio-kernel/config/sunautoconf.h)']
    fragment += ('ddk_module(\n    name = ' + json.dumps(name) + ',\n    srcs = '
                 + json.dumps(units + ['qcom/opensource/audio-kernel/config/sunautoconf.h'])
                 + ',\n    out = ' + json.dumps(name + '.ko')
                 + ',\n    kernel_build = "//common:kernel_aarch64",\n'
                 + '    kconfig = "Kconfig",\n    defconfig = "platform_defconfig",\n'
                 + '    deps = ' + json.dumps(deps) + ',\n    copts = ' + json.dumps(copts)
                 + ',\n    visibility = ["//visibility:public"],\n)\n\n')
(H / 'BUILD.fragment').write_text(fragment)
(S / 'BUILD.bazel').write_text('load("//build/kernel/kleaf:kernel.bzl", "ddk_headers", "ddk_module")\n\n' + fragment)
for name in ['Kconfig', 'platform_defconfig']:
    shutil.copyfile(P / name, S / name)
patch = ('Subject: Port native Onyx AW882xx and SIA91xx amplifiers\n\n'
         'Retain the native playback and tuning paths with checked registration\n'
         'ownership, resource cleanup and bounded userspace messages.\n\n---\n\n')
for path in paths:
    patch += ''.join(difflib.unified_diff((H / 'original' / path).read_text().splitlines(True),
        (H / 'private-source' / path).read_text().splitlines(True), fromfile='a/' + path, tofile='b/' + path))
(H / 'patch').write_text(patch)
print('Staged three native amplifier modules, 15 translation units')
