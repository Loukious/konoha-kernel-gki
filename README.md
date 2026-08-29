# konoha-kernel-gki — CI

CI-only repo: the workflows that build the Kono-Ha GKI kernel for POCO F7
(`onyx`) and the matching WLAN replacement module / vendor_dlkm image.
The kernel source itself lives on the `nethunter-ksunext-upstream` branch.

## Workflows (`.github/workflows/`)

| Workflow | What it builds | Trigger |
|---|---|---|
| `build-custom.yml` | The kernel itself (AnyKernel3 zip) + a `kernel-abi` artifact (Module.symvers, kernel.release, .config) so downstream module builds can match its ABI exactly | `workflow_dispatch` |
| `build-wlan-injection.yml` | The WCN7750 WLAN module (monitor mode + frame injection) rebuilt against a chosen kernel's ABI, then repacked into a PixelOS onyx vendor_dlkm EROFS image | `workflow_dispatch` or `workflow_call` (pass `kernel_run_id` to build against a specific kernel run) |
| `nethunter-release.yml` | Orchestrator: resolves/waits for a kernel run → calls the wlan workflow with that run's ABI → folds the vendor_dlkm image into the AnyKernel3 zip as a single recovery-flashable `*-wlan.zip` → publishes the release | `workflow_dispatch` |

## `tools/`

- `build_wlan_injection_module.sh` — external-module build of qca_cld3_wcn7750.ko
- `make_pixelos_wlan_symvers.sh` — vendor ABI symbols from the stock PixelOS modules
- `make_pixelos_vendor_dlkm_img.sh` — swap the module into a stock vendor_dlkm EROFS image (mkfs.erofs + AVB hashtree footer)
- `make_combined_ak3_zip.sh` — single-zip kernel + vendor_dlkm flasher
- `xiaomi-source-stubs/` — Kconfig/header stubs the ABI prep needs from the MiCode tree

## `docs/nethunter/`

Historic patches for the WLAN driver (packet injection port, stock-parity
series). The current wlan source
(`Loukious/vendor_qcom_opensource_wlan@onyx-v-oss-monitor-direct`) has
injection integrated; the workflows detect that and skip patching.

## Branches

- `main` — this CI tree
- `nethunter-ksunext-upstream` — the kernel source + the same workflows (the live copies dispatched from)
