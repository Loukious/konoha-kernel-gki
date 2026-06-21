#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_SRC="${KERNEL_SRC:-$ROOT_DIR}"
KERNEL_OUT="${KERNEL_OUT:-$ROOT_DIR/out}"
WLAN_TREE="${WLAN_TREE:-$ROOT_DIR/vendor/qcom/opensource/wlan}"
OUT_KO="${1:-$ROOT_DIR/artifacts/wlan/qca_cld3_wcn7750-konoha-injection.ko}"
PLATFORM_DIR="$WLAN_TREE/platform"
QCACLD_DIR="$WLAN_TREE/qcacld-3.0"
VENDOR_SYMVERS="${VENDOR_SYMVERS:-$ROOT_DIR/artifacts/wlan/vendor-abi/pixelos-qca-extra.symvers}"
JOBS="${JOBS:-$(nproc)}"
WLAN_CTRL_NAME="${WLAN_CTRL_NAME:-wlan}"
REQUIRE_INJECTION="${REQUIRE_INJECTION:-true}"

if [[ ! -f "$KERNEL_OUT/.config" || ! -f "$KERNEL_OUT/Module.symvers" ]]; then
	echo "Kernel output is incomplete: $KERNEL_OUT" >&2
	echo "Build the kernel before building the WLAN module." >&2
	exit 1
fi

if [[ ! -d "$PLATFORM_DIR" || ! -d "$QCACLD_DIR" ]]; then
	echo "WLAN source tree not found: $WLAN_TREE" >&2
	exit 1
fi

if ! grep -qx 'CONFIG_NL80211_TESTMODE=y' \
	"$QCACLD_DIR/configs/sun_gki_wcn7750_defconfig"; then
	echo "The WCN7750 profile must retain CONFIG_NL80211_TESTMODE=y." >&2
	exit 1
fi

if command -v llvm-strip >/dev/null 2>&1; then
	STRIP_BIN=llvm-strip
elif command -v llvm-strip-14 >/dev/null 2>&1; then
	STRIP_BIN=llvm-strip-14
else
	echo "llvm-strip is required." >&2
	exit 1
fi

COMMON_ARGS=(
	ARCH=arm64
	SUBARCH=arm64
	CC="${CC:-clang}"
	LLVM=1
	LLVM_IAS=1
	CONFIG_CNSS_OUT_OF_TREE=y
	"KCFLAGS=-w -D__ANDROID_COMMON_KERNEL__ -DCONFIG_ARM_SMMU_MODULE=1 -DCONFIG_CFG80211_MODULE=1 -DCONFIG_NL80211_TESTMODE=1 -DCONFIG_QCOM_IOMMU_UTIL_MODULE=1 -DCONFIG_QCOM_VA_MINIDUMP_MODULE=1 -DCONFIG_SCHED_WALT_MODULE=1"
)

PLATFORM_ARGS=(
	WLAN_PLATFORM_ROOT="$PLATFORM_DIR"
	CONFIG_CNSS2=m
	CONFIG_ICNSS2=m
	CONFIG_CNSS2_QMI=y
	CONFIG_ICNSS2_QMI=y
	CONFIG_CNSS2_DEBUG=y
	CONFIG_ICNSS2_DEBUG=y
	CONFIG_CNSS_QMI_SVC=m
	CONFIG_CNSS_PLAT_IPC_QMI_SVC=m
	CONFIG_CNSS_GENL=m
	CONFIG_WCNSS_MEM_PRE_ALLOC=m
	CONFIG_CNSS_UTILS=m
	CONFIG_CNSS2_SSR_DRIVER_DUMP=y
)

if [[ -f "$VENDOR_SYMVERS" ]]; then
	echo "[+] Using PixelOS vendor ABI symbols: $VENDOR_SYMVERS"
	EXTRA_SYMVERS="$VENDOR_SYMVERS"
else
	echo "[+] Building Qualcomm WLAN platform dependencies"
	make -j"$JOBS" -C "$KERNEL_SRC" O="$KERNEL_OUT" M="$PLATFORM_DIR" modules \
		"${COMMON_ARGS[@]}" "${PLATFORM_ARGS[@]}"
	EXTRA_SYMVERS="$PLATFORM_DIR/Module.symvers"
fi

echo "[+] Building WCN7750 driver"
make -j"$JOBS" -C "$KERNEL_SRC" O="$KERNEL_OUT" M="$QCACLD_DIR" modules \
	"${COMMON_ARGS[@]}" \
	WLAN_ROOT="$QCACLD_DIR" \
	WLAN_PROFILE=sun_gki_wcn7750 \
	MODNAME=qca_cld3_wcn7750 \
	CHIP_NAME=wcn7750 \
	DEVNAME=wcn7750 \
	WLAN_CTRL_NAME="$WLAN_CTRL_NAME" \
	QCA_WIFI_FTM_NL80211=y \
	CONFIG_QCA_WIFI_ISOC=0 \
	CONFIG_QCA_WIFI_2_0=1 \
	CONFIG_QCA_CLD_WLAN=m \
	CONFIG_ARM_SMMU=m \
	CONFIG_CFG80211=m \
	CONFIG_QCOM_IOMMU_UTIL=m \
	CONFIG_QCOM_VA_MINIDUMP=m \
	CONFIG_SCHED_WALT=m \
	KBUILD_EXTRA_SYMBOLS="$EXTRA_SYMVERS"

BUILT_KO="$QCACLD_DIR/qca_cld3_wcn7750.ko"
mkdir -p "$(dirname "$OUT_KO")"
cp "$BUILT_KO" "$OUT_KO"
"$STRIP_BIN" --strip-debug "$OUT_KO"

if [[ "$(modinfo -F name "$OUT_KO")" != "qca_cld3_wcn7750" ]]; then
	echo "Unexpected module name in $OUT_KO" >&2
	exit 1
fi

case "$REQUIRE_INJECTION" in
	true|1|yes)
		if ! grep -a -q 'hdd_monitor_mode_tx_inject' "$OUT_KO"; then
			echo "Injection entry point is missing from $OUT_KO" >&2
			exit 1
		fi
		;;
	false|0|no)
		;;
	*)
		echo "Invalid REQUIRE_INJECTION value: $REQUIRE_INJECTION" >&2
		exit 2
		;;
esac

if ! grep -a -q 'qcwlanstate' "$OUT_KO"; then
	echo "Android /dev/$WLAN_CTRL_NAME state control is missing from $OUT_KO" >&2
	exit 1
fi

if ! grep -a -q 'wlan/qca_cld/wcn7750/WCNSS_qcom_cfg.ini' "$OUT_KO"; then
	echo "PixelOS WCN7750 INI resource prefix is missing from $OUT_KO" >&2
	exit 1
fi

echo "[+] Created: $OUT_KO"
echo "    vermagic: $(modinfo -F vermagic "$OUT_KO")"
echo "    sha256:  $(sha256sum "$OUT_KO" | awk '{print $1}')"
