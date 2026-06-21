#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_ROOT="${MODULE_ROOT:-$ROOT_DIR/sources/pixelos/android_device_xiaomi_onyx-kernel/modules}"
KERNEL_SYMVERS="${KERNEL_SYMVERS:-$ROOT_DIR/out/Module.symvers}"
OUT_FILE="${1:-$ROOT_DIR/artifacts/wlan/vendor-abi/pixelos-qca-extra.symvers}"
STOCK_WIFI="$MODULE_ROOT/vendor_dlkm/qca_cld3_wcn7750.ko"

for tool in modprobe nm awk sed sort; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "Missing required tool: $tool" >&2
		exit 1
	fi
done

if [[ ! -f "$STOCK_WIFI" ]]; then
	echo "Stock PixelOS Wi-Fi module not found: $STOCK_WIFI" >&2
	exit 1
fi

if [[ ! -f "$KERNEL_SYMVERS" ]]; then
	echo "Kernel Module.symvers not found: $KERNEL_SYMVERS" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT_FILE")"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

modprobe --dump-modversions "$STOCK_WIFI" > "$WORK_DIR/imports.txt"

find "$MODULE_ROOT/vendor_boot" "$MODULE_ROOT/vendor_dlkm" \
	-maxdepth 1 -type f -name '*.ko' ! -name 'qca_cld3_wcn7750.ko' -print0 |
while IFS= read -r -d '' module; do
	provider="$(basename "$module" .ko)"
	nm -a "$module" 2>/dev/null |
		sed -n "s/.* __ksymtab_\(.*\)$/\1\t$provider/p"
done | sort -u -k1,1 > "$WORK_DIR/providers.txt"

awk -v kernel_file="$KERNEL_SYMVERS" \
	-v provider_file="$WORK_DIR/providers.txt" \
	-v missing="$WORK_DIR/missing.txt" '
	FILENAME == kernel_file { kernel[$2] = 1; next }
	FILENAME == provider_file {
		if (!provider[$1]) provider[$1] = $2
		next
	}
	{
		crc = $1
		symbol = $2
		if (kernel[symbol]) next
		if (provider[symbol])
			print crc "\t" symbol "\t" provider[symbol] "\tEXPORT_SYMBOL\t"
		else
			print symbol > missing
	}
' "$KERNEL_SYMVERS" "$WORK_DIR/providers.txt" "$WORK_DIR/imports.txt" > "$OUT_FILE"

if [[ -s "$WORK_DIR/missing.txt" ]]; then
	echo "Could not map these stock Wi-Fi imports:" >&2
	cat "$WORK_DIR/missing.txt" >&2
	exit 1
fi

echo "[+] Created: $OUT_FILE"
echo "    vendor symbols: $(wc -l < "$OUT_FILE")"
