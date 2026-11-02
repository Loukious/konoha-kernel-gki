"""Stage only this handoff. Invoke under flock /tmp/onyx-6.18-build.lock."""
import json
import shutil
from pathlib import Path

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
D = W / 'onyx_audio_small'
PREFIX = 'qcom/opensource/audio-kernel/'
CONFIG = '//onyx:' + PREFIX + 'config/sunautoconf.h'
sources = json.loads((H / 'sources.json').read_text())
D.mkdir(exist_ok=True)
for path in sources:
    dest = D / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(H / 'private-source' / path, dest)
for name, fragment in [('Kconfig', 'Kconfig.fragment'),
                       ('platform_defconfig', 'defconfig.fragment')]:
    (D / name).write_text((H / 'base-config' / name).read_text() + '\n' +
                          (H / fragment).read_text())

deps = {
    'pinctrl_lpi_dlkm': ['//onyx:audio_foundation_headers',
                         '//onyx:audio_notifier_headers', '//onyx:audio_spf_headers',
                         '//onyx:spf_core_dlkm', '//onyx:q6_notifier_dlkm',
                         '//onyx:snd_event_dlkm'],
    'sdca_registers_dlkm': [':audio_small_headers'],
    'swr_dmic_dlkm': ['//onyx_swr_irq:audio_swr_irq_headers', '//onyx:audio_wcdcore_headers',
                       '//onyx_swr_irq:swr_dlkm', '//onyx_wcd_codecs:audio_wcd_codecs_headers',
                       '//onyx_wcd_codecs:wcd939x_dlkm'],
    'swr_haptics_dlkm': ['//onyx_swr_irq:audio_swr_irq_headers', '//onyx:soc_headers',
                         '//onyx:remote_headers', '//onyx:qti_flash_led_headers',
                         '//onyx_swr_irq:swr_dlkm',
                         '//onyx:rproc_qcom_common'],
    'stub_dlkm': [],
    'hdmi_dlkm': ['//onyx_extdisp_audio:extdisp_audio_headers',
                   '//onyx_extdisp_audio:msm_ext_display'],
}
fragment = '''ddk_headers(
    name = "audio_small_headers",
    hdrs = ["qcom/opensource/audio-kernel/include/asoc/sdca-registers-api.h"],
    includes = ["qcom/opensource/audio-kernel/include"],
)
'''
for name in deps:
    evidence = json.loads((H / (name + '.json')).read_text())
    own = [p['path'] for p in evidence['pins'] if p['path'] in sources and
           p['path'] != PREFIX + 'include/asoc/sdca-registers-api.h']
    fragment += '''
ddk_module(
    name = %s,
    srcs = %s,
    out = %s,
    kernel_build = "//common:kernel_aarch64",
    kconfig = "Kconfig",
    defconfig = "platform_defconfig",
    deps = %s,
    copts = ["-include", "$(location %s)"],
)
''' % (json.dumps(name), json.dumps(own + [CONFIG]), json.dumps(name + '.ko'),
       json.dumps(deps[name]), CONFIG)

(H / 'BUILD.fragment').write_text(fragment)
(D / 'BUILD.bazel').write_text(
    'load("//build/kernel/kleaf:kernel.bzl", "ddk_module", "ddk_headers")\n'
    'package(default_visibility = ["//visibility:public"])\n\n' + fragment)
print(f'Staged {len(sources)} source/header paths and six native targets in {D}.')
