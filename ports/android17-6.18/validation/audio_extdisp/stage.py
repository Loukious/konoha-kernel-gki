"""Stage the isolated display provider. Call under the shared build flock."""
import json
import shutil
from pathlib import Path

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
D = W / 'onyx_extdisp_audio'
for path in json.loads((H / 'sources.json').read_text()):
    dest = D / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(H / 'private-source' / path, dest)
for name in ('Kconfig', 'platform_defconfig'):
    shutil.copyfile(H / 'base-config' / name, D / name)
fragment = '''ddk_headers(
    name = "extdisp_audio_headers",
    hdrs = ["qcom/opensource/mm-drivers/msm_ext_display/include/msm_ext_display.h"],
    includes = ["qcom/opensource/mm-drivers/msm_ext_display/include"],
)

ddk_module(
    name = "msm_ext_display",
    srcs = ["qcom/opensource/mm-drivers/msm_ext_display/src/msm_ext_display.c"],
    out = "msm_ext_display.ko",
    kernel_build = "//common:kernel_aarch64",
    kconfig = "Kconfig",
    defconfig = "platform_defconfig",
    deps = [":extdisp_audio_headers"],
)
'''
(H / 'BUILD.fragment').write_text(fragment)
(D / 'BUILD.bazel').write_text(
    'load("//build/kernel/kleaf:kernel.bzl", "ddk_headers", "ddk_module")\n'
    'package(default_visibility = ["//visibility:public"])\n\n' + fragment)
print('Staged isolated external display audio lifetime provider.')
