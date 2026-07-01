#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_SRC="${KERNEL_SRC:-$ROOT_DIR}"
KERNEL_OUT="${KERNEL_OUT:-$ROOT_DIR/out}"
WLAN_TREE="${WLAN_TREE:-$ROOT_DIR/vendor/qcom/opensource/wlan}"
OUT_KO="${1:-$ROOT_DIR/artifacts/wlan/icnss2-konoha-trace.ko}"
PLATFORM_DIR="$WLAN_TREE/platform"
JOBS="${JOBS:-$(nproc)}"
WLAN_EXTRA_KCFLAGS="${WLAN_EXTRA_KCFLAGS:-}"

if [[ ! -f "$KERNEL_OUT/.config" || ! -f "$KERNEL_OUT/Module.symvers" ]]; then
	echo "Kernel output is incomplete: $KERNEL_OUT" >&2
	exit 1
fi

if [[ ! -d "$PLATFORM_DIR" ]]; then
	echo "WLAN platform source tree not found: $PLATFORM_DIR" >&2
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
	"KCFLAGS=-w -mno-outline -D__ANDROID_COMMON_KERNEL__ -DCONFIG_ARM_SMMU_MODULE=1 -DCONFIG_CFG80211_MODULE=1 -DCONFIG_NL80211_TESTMODE=1 -DCONFIG_QCOM_IOMMU_UTIL_MODULE=1 -DCONFIG_QCOM_MINIDUMP_MODULE=1 -DCONFIG_QCOM_VA_MINIDUMP_MODULE=1 -DCONFIG_SCHED_WALT_MODULE=1 $WLAN_EXTRA_KCFLAGS"
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

echo "[+] Building Qualcomm WLAN platform modules"
make -j"$JOBS" -C "$KERNEL_SRC" O="$KERNEL_OUT" M="$PLATFORM_DIR" modules \
	"${COMMON_ARGS[@]}" "${PLATFORM_ARGS[@]}"

BUILT_KO="$PLATFORM_DIR/icnss2/icnss2.ko"
mkdir -p "$(dirname "$OUT_KO")"
cp "$BUILT_KO" "$OUT_KO"
"$STRIP_BIN" --strip-debug "$OUT_KO"

if [[ "$(modinfo -F name "$OUT_KO")" != "icnss2" ]]; then
	echo "Unexpected module name in $OUT_KO" >&2
	exit 1
fi

echo "[+] Created: $OUT_KO"
echo "    vermagic: $(modinfo -F vermagic "$OUT_KO")"
echo "    sha256:  $(sha256sum "$OUT_KO" | awk '{print $1}')"
