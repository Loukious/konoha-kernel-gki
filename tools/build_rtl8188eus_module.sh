#!/usr/bin/env bash
set -euo pipefail

# Build the Loukious/rtl8188eus@konoha driver (Realtek RTL8188EUS USB
# adapter, monitor mode + frame injection) as a module against the prepared
# konoha-ABI kernel output.
#
# The fork is gglluukk upstream v5.3.9 plus everything konoha needs, baked
# into the branch — this script does NOT patch the driver source:
#   - STA-ALIGN8: the sta_info pool is 8-byte aligned. Upstream hand-aligns
#     it to 4, and on CONFIG_LTO arm64 kernels the kernel's READ_ONCE
#     compiles to an LDAR, which raises an alignment fault on a 4-mod-8
#     timer->entry.pprev during teardown — the plug-freeze panic,
#     reproduced and fixed 2026-09-13.
#   - MODULE_IMPORT_NS(VFS_internal...): GKI kernels export kernel_read and
#     file-IO symbols in that namespace; modpost fails without it.
#   - clang-hostile -Wno-* flags dropped so LLVM=1 builds pass.
#
# The build invocation is the recipe CI-validated against the gglluukk
# lineage (2026-09-13): kbuild-style make from the kernel source with
# M=$DRIVER_SRC, KCFLAGS appended last so -Wno-error beats the kernel's
# Werror. The tree's own Makefile self-enables -DCONFIG_IOCTL_CFG80211
# -DRTW_USE_CFG80211_STA_EVENT, so no defines are passed here.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_SRC="${KERNEL_SRC:-$ROOT_DIR/sources/xiaomi-kernel}"
KERNEL_OUT="${KERNEL_OUT:-$ROOT_DIR/out}"
DRIVER_SRC="${DRIVER_SRC:-$ROOT_DIR/sources/rtl8188eus}"
EXTRA_SYMVERS="${EXTRA_SYMVERS:-$ROOT_DIR/artifacts/wlan/vendor-abi/pixelos-qca-extra.symvers}"
OUT_KO="${1:-$ROOT_DIR/artifacts/wlan/8188eu.ko}"
JOBS="${JOBS:-$(nproc)}"

for var in KERNEL_SRC KERNEL_OUT DRIVER_SRC EXTRA_SYMVERS; do
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

# The driver tree must be the konoha fork, not pristine upstream: upstream
# still carries the 4-byte sta_info alignment that panics LTO arm64 kernels
# on every registered-adapter unbind.
fork_checks=(
	'STA-ALIGN8:core/rtw_sta_mgt.c'
	'MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver):os_dep/linux/os_intfs.c'
)
for check in "${fork_checks[@]}"; do
	marker="${check%%:*}"
	file="${check#*:}"
	if ! grep -q "$marker" "$DRIVER_SRC/$file"; then
		echo "$DRIVER_SRC is not the konoha fork: $file lacks \"$marker\"" >&2
		echo "Clone the fork instead of patching:" >&2
		echo "  git clone --depth=1 -b konoha https://github.com/Loukious/rtl8188eus.git" >&2
		exit 1
	fi
done

# Optional debug logging (RTW_DEBUG=1). The driver is SILENT by default:
# with CONFIG_RTW_DEBUG unset, include/rtw_debug.h compiles every logging
# macro - RTW_PRINT/ERR/WARN/INFO/DBG, error paths included - down to
# "do {} while (0)", so a released module cannot report anything at all.
# Turning it on needs BOTH switches: the Makefile only adds the defines
# under `ifeq ($(CONFIG_RTW_DEBUG), y)`, and the level it bakes in comes
# from CONFIG_RTW_LOG_LEVEL, whose in-tree default of 0 (_DRV_NONE_) is
# still silent. Level 4 is _DRV_INFO_ (5 = _DRV_DEBUG_); once built this
# way, rtw_drv_log_level is also a module_param(0644) and so retunable at
# insmod time and at runtime without another build.
debug_args=()
case "${RTW_DEBUG:-0}" in
	1 | y | yes | on)
		debug_args=(
			CONFIG_RTW_DEBUG=y
			"CONFIG_RTW_LOG_LEVEL=${RTW_LOG_LEVEL:-4}"
		)
		echo "[+] Debug logging ENABLED (level ${RTW_LOG_LEVEL:-4}) - debug build, not for release"
		;;
	*)
		echo "[+] Debug logging disabled (release build; pass RTW_DEBUG=1 to enable)"
		;;
esac

echo "[+] Building 8188eu from the konoha fork (jobs: $JOBS)"
make -j"$JOBS" -C "$KERNEL_SRC" O="$KERNEL_OUT" M="$DRIVER_SRC" \
	ARCH=arm64 LLVM=1 LLVM_IAS=1 \
	KBUILD_EXTRA_SYMBOLS="$EXTRA_SYMVERS" \
	CONFIG_RTL8188EU=m \
	${debug_args[@]+"${debug_args[@]}"} \
	KCFLAGS="-Wno-error -Wno-unknown-warning-option" \
	modules

BUILT_KO="$DRIVER_SRC/8188eu.ko"
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
		echo "8188eu vermagic does not match the prepared kernel" >&2
		echo "  kernel release:  $kernel_release" >&2
		echo "  module vermagic: $module_vermagic" >&2
		exit 1
		;;
esac

if [[ "$(modinfo -F depends "$OUT_KO")" != "cfg80211" ]]; then
	echo "Unexpected 8188eu dependencies: $(modinfo -F depends "$OUT_KO")" >&2
	exit 1
fi

# Prove the build is what it claims rather than trusting it. String
# literals survive llvm-strip, so their presence in the binary is a direct
# read of what the module actually contains.
#   - STA-ALIGN8 print (debug builds only): the marker is an RTW_INFO
#     macro, which compiles away entirely in release builds. The fix
#     itself is unconditional code already guaranteed by the source-level
#     fork checks above; in a debug build the marker must be present.
if [[ ${#debug_args[@]} -gt 0 ]] && ! grep -aqF "STA-ALIGN8" "$OUT_KO"; then
	echo "Debug build lacks the STA-ALIGN8 marker - wrong driver tree?" >&2
	exit 1
fi

#   - Debug logging: when the macros are compiled out their format strings
#     vanish from .rodata entirely.
log_marker="nr_endpoint="
if [[ ${#debug_args[@]} -gt 0 ]]; then
	if ! grep -aqF "$log_marker" "$OUT_KO"; then
		echo "RTW_DEBUG was requested but the module carries no log strings" >&2
		exit 1
	fi
	log_state="enabled (level ${RTW_LOG_LEVEL:-4})"
else
	if grep -aqF "$log_marker" "$OUT_KO"; then
		echo "Release build unexpectedly carries debug log strings" >&2
		exit 1
	fi
	log_state="compiled out"
fi

echo "[+] Created: $OUT_KO"
echo "    vermagic:  $module_vermagic"
echo "    logging:   $log_state"
echo "    size:      $(stat -c %s "$OUT_KO") bytes"
