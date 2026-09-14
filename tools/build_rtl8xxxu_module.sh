#!/usr/bin/env bash
set -euo pipefail

# Build the in-tree rtl8xxxu driver (mainline Realtek USB wifi family:
# RTL8188CU/8192CU, 8188EU, 8188FU, 8192EU, 8723AU/BU, 8192FU, 8710BU)
# as a module against the prepared konoha-ABI kernel output.
#
# This is the driver for the 0bda:8176 (RTL8188CU/8192CU) dongles that
# the 8188eu vendor driver does NOT support: 8176 is a different chip
# family, and force-binding 8188eu to it fails the radio power-on poll,
# reads a blank EFUSE (0xFFFF / random MAC) and drops the device off
# the bus after ~2s (diagnosed on onyx 2026-09-14).
#
# Two defines are load-bearing; neither is optional:
#   - CONFIG_RTL8XXXU_UNTESTED: the 0bda:8176 USB ID (and the other
#     vendor-variant IDs) live in the driver's "untested" device table;
#     without this define the module does not claim the device at all.
#   - CONFIG_NL80211_TESTMODE: the phone's mac80211 is the ROM VENDOR
#     module (/vendor_dlkm/lib/modules/mac80211.ko), built with
#     TESTMODE=y, so its struct ieee80211_ops carries two extra members
#     (testmode_cmd/testmode_dump, slots 55/56). Compiling this driver
#     against a TESTMODE-less config puts wake_tx_queue at 0x2e0
#     instead of the vendor's 0x2f0; the vendor's
#     ieee80211_alloc_hw_nm then reads NULL there and every probe dies
#     with -ENOMEM after a WARN at net/mac80211/main.c:660. Diagnosed
#     by disassembling the vendor module 2026-09-14; the 0x2f0 offset
#     check below asserts the layout at build time so a config drift
#     fails here instead of on the device.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_SRC="${KERNEL_SRC:-$ROOT_DIR/sources/xiaomi-kernel}"
KERNEL_OUT="${KERNEL_OUT:-$ROOT_DIR/out}"
# NOTE: no KBUILD_EXTRA_SYMBOLS. The qcacld extra symvers are the 8188eu
# recipe; this module resolved every cfg80211/mac80211 symbol from the
# ABI-prep Module.symvers, and that exact build loaded and ran on the
# device (2026-09-14). Changing the symbol source changes the baked
# modversions CRCs - keep the recipe identical to the validated one.
DRIVER_SRC="${DRIVER_SRC:-$KERNEL_SRC/drivers/net/wireless/realtek/rtl8xxxu}"
OUT_KO="${1:-$ROOT_DIR/artifacts/wlan/rtl8xxxu.ko}"
JOBS="${JOBS:-$(nproc)}"

for var in KERNEL_SRC KERNEL_OUT DRIVER_SRC; do
	if [[ ! -e "${!var}" ]]; then
		echo "Missing $var: ${!var}" >&2
		exit 1
	fi
done
if [[ ! -f "$KERNEL_OUT/.config" || ! -f "$KERNEL_OUT/Module.symvers" ]]; then
	echo "Kernel output is incomplete: $KERNEL_OUT" >&2
	echo "Run the ABI prep (modules_prepare) before building this driver." >&2
	exit 1
fi

echo "[+] Building rtl8xxxu from the kernel tree (jobs: $JOBS)"
# -fdata-sections is load-bearing for the ieee80211_ops layout check below:
# it puts rtl8xxxu_ops in a dedicated .rodata.rtl8xxxu_ops section so the
# wake_tx_queue relocation offset equals the struct slot offset. The CI ABI
# tree (defconfig merge) has CONFIG_LTO off, where nothing adds section
# splitting and the ops struct would merge into plain .rodata; the locally
# validated build had LTO on, whose ld -r splits sections implicitly. Ask
# for it explicitly so the check works on either tree.
make -j"$JOBS" -C "$KERNEL_SRC" O="$KERNEL_OUT" M="$DRIVER_SRC" \
	ARCH=arm64 LLVM=1 LLVM_IAS=1 \
	CONFIG_RTL8XXXU=m \
	KCFLAGS="-DCONFIG_RTL8XXXU_UNTESTED -DCONFIG_NL80211_TESTMODE -fdata-sections -Wno-error -Wno-unknown-warning-option" \
	modules

BUILT_KO="$DRIVER_SRC/rtl8xxxu.ko"
if [[ ! -f "$BUILT_KO" ]]; then
	echo "Driver did not produce $BUILT_KO" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT_KO")"
if command -v llvm-strip >/dev/null 2>&1; then
	STRIP_BIN=llvm-strip
else
	STRIP_BIN=strip
fi
"$STRIP_BIN" --strip-unneeded "$BUILT_KO" -o "$OUT_KO"

kernel_release="$(cat "$KERNEL_OUT/include/config/kernel.release")"
module_vermagic="$(modinfo -F vermagic "$OUT_KO")"
case "$module_vermagic" in
	"$kernel_release"*) ;;
	*)
		echo "rtl8xxxu vermagic does not match the prepared kernel" >&2
		echo "  kernel release:  $kernel_release" >&2
		echo "  module vermagic: $module_vermagic" >&2
		exit 1
		;;
esac

if [[ "$(modinfo -F depends "$OUT_KO")" != "cfg80211,mac80211" ]]; then
	echo "Unexpected rtl8xxxu dependencies: $(modinfo -F depends "$OUT_KO")" >&2
	exit 1
fi

# The UNTESTED table must be compiled in: without it the module never
# claims 0bda:8176 (our validated dongle ID).
if ! modinfo "$OUT_KO" | grep -q '^alias: *usb:v0BDAp8176d\*'; then
	echo "Module lacks the v0BDAp8176 alias - CONFIG_RTL8XXXU_UNTESTED missing?" >&2
	exit 1
fi

# Firmware the driver requests must be shipped in the module zip
# (tools/firmware/). Assert a few known ones so a driver update that
# renames firmware fails here rather than on the device.
for fw in rtlwifi/rtl8192cufw_TMSC.bin rtlwifi/rtl8188eufw.bin rtlwifi/rtl8192eu_nic.bin; do
	if ! modinfo -F firmware "$OUT_KO" | grep -qx "$fw"; then
		echo "Module no longer requests $fw - update tools/firmware/" >&2
		exit 1
	fi
done

# The ieee80211_ops layout must match the ROM vendor mac80211 module:
# its ieee80211_alloc_hw_nm reads wake_tx_queue at offset 0x2f0 (see
# header comment). In the unlinked object rtl8xxxu_ops is a dedicated
# section (compiler -fdata-sections), so the relocation offset can be
# checked directly. A mismatch here means the vendor mac80211 ABI changed
# (or TESTMODE drifted) - do not ship, re-derive the layout from the
# vendor module.
OBJDUMP=""
for cand in llvm-objdump aarch64-linux-gnu-objdump objdump; do
	if command -v "$cand" >/dev/null 2>&1; then
		OBJDUMP="$cand"
		break
	fi
done
if [[ -z "$OBJDUMP" ]]; then
	echo "No objdump available for the ieee80211_ops layout check" >&2
	exit 1
fi
# Dump every relocation and scope to the ops section by group header.
# Deliberately NOT "objdump -r -j <section>": the -j section name is
# tool-dependent (GNU objdump wants the TARGET section .rodata.rtl8xxxu_ops,
# llvm-objdump the .rela.rodata.rtl8xxxu_ops relocation section), and the
# wrong form silently prints nothing - which is exactly how the first CI
# run of this check failed (2026-09-14, run 34836547402) while the local
# llvm build passed.
reloc_dump="$("$OBJDUMP" -r "$DRIVER_SRC/rtl8xxxu.o" 2>/dev/null || true)"
if [[ -z "$reloc_dump" ]]; then
	echo "Could not read relocations from $DRIVER_SRC/rtl8xxxu.o with $OBJDUMP" >&2
	exit 1
fi
ops_relocs="$(printf '%s\n' "$reloc_dump" \
	| awk '/^RELOCATION RECORDS FOR \[\.rodata\.rtl8xxxu_ops\]/ {inrel = 1; next}
		/^RELOCATION RECORDS/ {inrel = 0}
		inrel')"
if [[ -z "$ops_relocs" ]]; then
	echo "No .rodata.rtl8xxxu_ops section in the unlinked object." >&2
	echo "The build passes -fdata-sections to create it - without the dedicated" >&2
	echo "section the ops struct merges into .rodata and its slot offsets cannot" >&2
	echo "be checked (this is how CI runs 34836547402/34839284156 failed before" >&2
	echo "the flag was added: the ABI tree's defconfig merge has CONFIG_LTO off," >&2
	echo "so nothing split the sections)." >&2
	exit 1
fi
# The driver references ieee80211_handle_wake_tx_queue exactly once, in the
# ops struct - assert that so a driver update adding a second reference
# fails here instead of silently weakening the check.
ref_count="$(printf '%s\n' "$ops_relocs" | grep -c 'ieee80211_handle_wake_tx_queue' || true)"
if [[ "$ref_count" != "1" ]]; then
	echo "Expected exactly 1 ieee80211_handle_wake_tx_queue reference in the ops section, found $ref_count" >&2
	exit 1
fi
if ! printf '%s\n' "$ops_relocs" \
	| grep -q '^[[:space:]]*00000000000002f0[[:space:]].*ieee80211_handle_wake_tx_queue'; then
	echo "ieee80211_ops layout mismatch: wake_tx_queue is not at 0x2f0" >&2
	echo "The ROM vendor mac80211 expects it at 0x2f0 (CONFIG_NL80211_TESTMODE=y," >&2
	echo "CONFIG_MAC80211_DEBUGFS off). Re-derive from /vendor_dlkm/lib/modules/mac80211.ko." >&2
	exit 1
fi

echo "[+] Created: $OUT_KO"
echo "    vermagic:            $module_vermagic"
echo "    wake_tx_queue:       0x2f0 (vendor mac80211 layout OK)"
echo "    firmware requested:  $(modinfo -F firmware "$OUT_KO" | wc -l) files"
echo "    size:                $(stat -c %s "$OUT_KO") bytes"
