#!/usr/bin/env bash
set -euo pipefail

# Build the SimplyCEO/rtl8188eus out-of-tree driver (Realtek RTL8188EUS USB
# adapter, monitor mode + frame injection) as a module against the prepared
# konoha-ABI kernel output.
#
# Everything this script does was validated locally against the same kind of
# modules_prepare'd tree (2026-09-11):
#   - the driver's CONFIG_PLATFORM_I386_PC wrapper supplies the needed
#     -DCONFIG_IOCTL_CFG80211 define set (it is a build-system selector, not
#     an architecture statement; ARCH/KSRC are overridden on the command line)
#   - clang needs -Wno-error: rtw_recv.h has two -Wuninitialized warnings
#     upstream never silenced
#   - GKI kernels export filp_open/kernel_read inside the
#     VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver namespace;
#     MODULE_IMPORT_NS must be added to os_intfs.c (idempotent)
#   - cfg80211 comes from a vendor module, so its CRCs resolve through the
#     same extra-symvers file the qcacld build uses (KBUILD_EXTRA_SYMBOLS)

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

# The namespace import. MODULE_IMPORT_NS takes the namespace unquoted
# (it stringifies internally) - see include/linux/module.h.
if ! grep -q 'MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver)' \
	"$DRIVER_SRC/os_dep/linux/os_intfs.c"; then
	patch -s -p1 -d "$DRIVER_SRC" <<'PATCH'
--- a/os_dep/linux/os_intfs.c
+++ b/os_dep/linux/os_intfs.c
@@ -28,0 +29,4 @@ MODULE_VERSION(DRIVERVERSION);
+/* GKI kernels export kernel_read/file-IO symbols inside the
+ * VFS_internal namespace; modules using them must import it. */
+MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
+
PATCH
	echo "[+] Added VFS_internal namespace import"
else
	echo "[+] VFS_internal namespace import already present"
fi

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

echo "[+] Building 8188eu (jobs: $JOBS)"
# The Realtek Makefile wraps an ordinary external-module build:
#   make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -C $KSRC M=$(pwd) modules
# Every KSRC/ARCH/toolchain variable passed here overrides the Makefile's
# own (command-line assignments win), which is how we reuse the I386_PC
# platform block (it only contributes the CONFIG_IOCTL_CFG80211 defines).
make -C "$DRIVER_SRC" -j"$JOBS" \
	${debug_args[@]+"${debug_args[@]}"} \
	ARCH=arm64 \
	KSRC="$KERNEL_OUT" \
	CC=clang \
	CROSS_COMPILE=llvm- \
	HOSTCC=clang \
	LD=ld.lld \
	AR=llvm-ar \
	NM=llvm-nm \
	STRIP=llvm-strip \
	OBJCOPY=llvm-objcopy \
	OBJDUMP=llvm-objdump \
	READELF=llvm-readelf \
	HOSTAR=llvm-ar \
	HOSTLD=ld.lld \
	KBUILD_EXTRA_SYMBOLS="$EXTRA_SYMVERS" \
	CONFIG_PLATFORM_I386_PC=y \
	USER_EXTRA_CFLAGS="-Wno-error -DCONFIG_LITTLE_ENDIAN -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT" \
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

# Prove the logging switch landed rather than trusting it. When the macros
# are compiled out their format strings vanish from .rodata entirely, so the
# presence of one is a direct read of what the binary can actually print.
# (llvm-strip drops symbols, not string literals, so this survives the strip.)
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
