# Main WCN7750 WLAN discovery

This is read-only preparation. No driver inputs were staged or changed, and no
6.18 main-driver compilation or hardware validation was performed. Reproduce
with `python3 discover.py`, `python3 api_diff.py`, then `python3 supplement.py`
from this directory. The scripts read the native receipts, selected Git pin and
current central inventory and write reports only here.

## Exact source and build selection

The native module receipt contains **813 distinct object/source entries**. All
813 corresponding `.o.cmd` receipts exist and were parsed. `objects.json` records
the original object path, source receipt, canonical source path and command hash.
The selected repository is `baseline/kernel/xiaomi/onyx-wlan`, pinned to
`6cc986b6e08783b8a14b155c1531c86959a1398a`.

`new-wlan-pins.json` contains **2,157 new pins** after excluding the central
snapshot: **814 C files and 1,343 headers**. Only the 813 `.mod` C files belong in
the linked object list: `qca-wifi-host-cmn/wmi/src/wmi_tlv_platform.c` is an extra
textually included C dependency and must not become a separate translation unit.
There are no unresolved WLAN/vendor receipt paths. `config-pins.json` separately
records the Kbuild, Makefile, Kconfig and three WCN7750 config sources as evidence.
Do not infer all 814 C files should be compiled from the import inventory.

Native `qcacld-3.0/cmn` is a symlink to `../qca-wifi-host-cmn`. The inventory
canonicalizes it and all `..` segments. Rewrite include paths to canonical
staged paths; do not import duplicate aliases. The receipts contain 269 ordered
include entries in one shared include group.

There are three define groups: 496 common distinct `-D` options, with two extra
per-object definitions. `WLAN_BCN_RATECODE_ENABLE` is added only for
`wmi_unified_tlv.c` and `wma_dev_if.c`; `CONFIG_WLAN_SUPPORT_LAPB` is added only for
`wlan_dp_flow_ucfg_api.c`. Full ordered lists and object membership are recorded.
The main translation unit force-includes native `configs/default_config.h` and
`qcom/opensource/dataipa/config/dataipa_vendor.h`, in addition to kernel compiler
headers. The former is in the new pin closure; the latter is already central.

The other already-central WLAN closure headers are `platform/inc/cnss_nl.h`,
`cnss_prealloc.h` and `cnss_utils.h`. `existing-provider-headers.json` records both
native and central pins. Four additional vendor kernel headers missing from stock
6.18 are already central: qcom-iommu-util, sched/walt, qcom/minidump and
qcom/secure_buffer. Use their established header targets. Standard kernel and
generated headers must come from 6.18 rather than importing the 947 old raw
kernel dependency paths. See `missing-common-headers.json` for renamed/removed
standard headers to review when porting direct includes.

The native command includes broad `-w` and old kernel/compiler flags. Preserve
WLAN feature semantics, but let Kleaf provide the 6.18 compiler/ABI flags and
keep useful build diagnostics. Re-evaluate the source Kbuild's header feature
probes against 6.18; the captured `CFG80211_*` defines describe the 6.6 header,
not a valid 6.18 capability manifest.

## Monitor and injection selection

All 813 receipts enable `FEATURE_MONITOR_MODE_SUPPORT`,
`FEATURE_FRAME_INJECTION_SUPPORT`, `WIFI_MONITOR_SUPPORT`,
`QCA_MONITOR_PKT_SUPPORT`, `CONFIG_WLAN_SYSFS_MONITOR_MODE_CHANNEL`,
`QCA_WIFI_MONITOR_MODE_NO_MSDU_START_TLV_SUPPORT`, `DIRECT_BUF_RX_ENABLE`,
`WLAN_CFR_ENABLE` and `WLAN_ENH_CFR_ENABLE`. WCN7750 and 11be/MLO features are also
selected. `feature-defines.json` records every matching option and its objects.
The selected `configs/wcn7750_defconfig:672` explicitly enables monitor and frame
injection support. There is no need to invent a separate direct-injection flag.

The selected `wlan_hdd_tx_rx.c:496` includes the custom monitor transmit path,
radiotap parsing/FCS stripping, QDF control-block reset and `wma_injection_tx()`
dispatch. Its current frame validator deliberately accepts management frames;
this is not evidence of arbitrary raw data/control-frame injection. Preserve the
selected implementation and validate its behavior during the eventual port.

Current native working-tree bytes match 2,155 of the 2,157 selected closure pins.
Two direct-buffer RX files differ: native working files add `buffer_capacity` and
reject/repost a firmware release length beyond its allocation. This is recorded
in `native-source-comparison.json`; the selected source pin lacks those edits.
Do not silently replace the selected pin or import working-tree changes as if
pinned. Receipts prove object membership/options, not the historical compiler's
source bytes. Review these DBR bounds changes separately with provenance.

## Transport evidence: ICNSS2, generic PCI symbols and host drivers

The native `qca_cld3_wcn7750.ko` depends on **icnss2**, not cnss2. Receipts select
`HIF_IPCI` and `CONFIG_PLD_IPCI_ICNSS`, and actually link `if_ipci.c`,
`multibus_ipci.c`, `pld_ipci.c` and the shared `pld_common.c`. They do not link the
standard HIF `pcie/if_pci.c`. This makes ICNSS2 the actual platform-provider work
needed for the selected main WLAN build.

`pld_ipci_register_driver()` calls `icnss_register_driver()` at
`qcacld-3.0/core/pld/src/pld_ipci.c:548`; its SoC discovery calls
`icnss_get_soc_info()` at line 654. The HIF IPCI bus-enable path uses the passed
`struct device`, DMA mask, PLD SoC information and register tables.
`platform/icnss2/Kconfig` describes Q6 integrated iWCN and has no PCI dependency.
ICNSS2 is a platform driver; its WCN7750 path obtains MMIO information from
firmware and maps it with `devm_ioremap()` (`main.c:1334`), alongside DT resources.
Names such as BAR and MHI state-information region do not establish Linux PCI
host or MHI controller registration.

The distinction is explicit in `transport-evidence.json`:

- Main WLAN imports four generic built-in PCI core symbols:
  `pci_read_config_word`, `pci_write_config_word`, `pci_read_config_dword`,
  `pci_write_config_dword`. It imports no `pcie_*` or `mhi_*` symbol.
- Those four references come from linked `pld_common.c:2006/2033/2059/2085`.
  Each calls the PCI helper only for `PLD_BUS_TYPE_PCIE`; its
  `PLD_BUS_TYPE_IPCI` case explicitly breaks without that call.
- Native ICNSS2 imports no `pci_*`, `pcie_*` or `mhi_*` symbol. Its dependencies
  are logging/ramdump, socinfo, PDR/remoteproc, prealloc, IOMMU, pinctrl, AOSS,
  firmware service and QMI helpers. The selected IPCI/ICNSS2 route sources contain
  no direct calls to these PCI/MHI families.

Therefore the exact native main-module composition needs generic PCI core symbol
availability at link/load time. These facts do **not** demonstrate a runtime
requirement for the standard PCIe root driver (`PCI_MSM`) or Linux MHI host for
this WCN7750 route. Do not infer that requirement from the IPCI name or confuse it
with the independent CNSS2/PCIe port. Hardware topology/DT and later runtime
validation remain necessary for any broader removal decision.

## 6.18 adaptation work identified

`cfg80211-ops-diff.json` compares the real native ROM and pinned 6.18 headers for
all assigned driver operations. Eighteen callback signatures differ; this is an
API-change inventory, not eighteen proven remaining compiler errors. The source
already handles the 6.7 `change_beacon(cfg80211_ap_update *)` change in
`wlan_hdd_hostapd.c:9333`. The following gaps still need explicit adaptation:

1. `set_monitor_channel`: the current wrapper at `wlan_hdd_cfg80211.c:31348`
   accepts only `(wiphy, chandef)`; 6.18 adds `struct net_device *dev`. Add the
   correctly typed wrapper and validate/select the requested monitor device
   while retaining the intended fallback for caller contexts without a device.
   Preserve the existing channel/regulatory checks and monitor context update;
   do not merely cast the callback pointer. Root's framework policy remains a
   separate already-reviewed change.
2. Station operations and add/get/delete/default-management/default-beacon key
   operations now take `wireless_dev *` where the native callbacks take
   `net_device *`. Adapt wrappers to the real driver-owned device and preserve
   link IDs. Simply enabling `CFG80211_KEY_INSTALL_SUPPORT_ON_WDEV` is insufficient:
   those older vendor branches omit `link_id` and use a legacy `-1` path.
3. Wiphy params, TX power and antenna callbacks add `radio_idx`; get-TX-power
   additionally adds `link_id`. Map supported radio/link selection to actual
   driver state and reject unsupported values instead of silently discarding it.
4. Remain-on-channel adds `rx_addr`; preserve the interface's requested receive
   address semantics or return an appropriate unsupported error.
5. Notification APIs also change. New/del-station notifications use `wdev`;
   association response uses `cfg80211_rx_assoc_resp_data`. Channel-switch
   notifications move puncturing into `chandef` and remove the extra argument.
   The selected shared `wlan_cfg80211.h` already has the correct
   `CFG80211_RU_PUNC_CHANDEF` path; Kbuild:3553 detects it from new mac80211 headers.
   Re-evaluate that probe instead of freezing the native `RU_PUNCT_NOTIFY` choice.
6. Shared QDF timer code still uses `from_timer`/`del_timer_sync`; the pinned
   6.18 header provides `timer_container_of`/`timer_delete_sync`. Direct includes
   of `asm/unaligned.h` in monitor TX and p2phc need `linux/unaligned.h` on 6.18;
   `os_if_wifi_pos.c` also directly includes removed `linux/genetlink.h`.

The next implementation should use the exact 813-object list, canonical source
closure, current providers and all selected monitor/injection features, then
compile and assess further cfg80211/data-structure and QDF API changes. This
bounded report does not replace compilation or protocol/lifetime tests.
