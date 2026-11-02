from pathlib import Path
import difflib
import hashlib
import json
import shutil

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
D = W / 'onyx_audio_speakers'
D.mkdir(exist_ok=True)
paths = json.loads((H / 'sources.json').read_text())
for path in paths:
    dest = D / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(H / 'private-source' / path, dest)
for name in ['Kconfig', 'platform_defconfig']:
    shutil.copyfile(P / name, D / name)
providers = json.loads((H / 'provider-dependencies.json').read_text())
headers = [p for p in paths if p.endswith('.h')]
fragment = '''ddk_headers(
    name = "audio_wsa_codec_headers",
    hdrs = %s,
    includes = ["qcom/opensource/audio-kernel/include", "qcom/opensource/audio-kernel/asoc"],
    visibility = ["//visibility:public"],
)

''' % json.dumps(headers)
for target, units in json.loads((H / 'units.json').read_text()).items():
    srcs = units + ['//onyx:qcom/opensource/audio-kernel/config/sunautoconf.h']
    deps = [':audio_wsa_codec_headers', '//onyx:audio_foundation_headers',
            providers['headers'], '//onyx:audio_wcdcore_headers', '//onyx:soc_headers',
            providers['module'], '//onyx:wcd_core_dlkm', '//onyx:qti_regmap_debugfs']
    fragment += '''ddk_module(
    name = "%s",
    srcs = %s,
    out = "%s.ko",
    kernel_build = "//common:kernel_aarch64",
    kconfig = "Kconfig",
    defconfig = "platform_defconfig",
    deps = %s,
    copts = ["-include", "$(location //onyx:qcom/opensource/audio-kernel/config/sunautoconf.h)"],
    visibility = ["//visibility:public"],
)

''' % (target, json.dumps(srcs), target, json.dumps(deps))
(D / 'BUILD.bazel').write_text('load("//build/kernel/kleaf:kernel.bzl", "ddk_headers", "ddk_module")\n\n' + fragment)
(H / 'BUILD.fragment').write_text(fragment)
for name in ['Kconfig.fragment', 'defconfig.fragment']:
    (H / name).write_text('# Native WSA883x/WSA884x selections come from frozen sunautoconf.h.\n')
(H / 'patch').write_text('Subject: Port native WSA883x and WSA884x codecs to 6.18\n\n'
    'Retain native speaker interfaces; drain resources before teardown.\n\n---\n\n' + ''.join(
        ''.join(difflib.unified_diff((H / 'original' / p).read_text().splitlines(True),
                                    (H / 'private-source' / p).read_text().splitlines(True),
                                    fromfile='a/' + p, tofile='b/' + p)) for p in paths))
print('Staged native WSA883x and WSA884x codecs')
