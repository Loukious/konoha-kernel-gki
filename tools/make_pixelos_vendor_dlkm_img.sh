#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_DIR="$ROOT_DIR/sources/pixelos/android_device_xiaomi_onyx-kernel/modules/vendor_dlkm"
OUT_IMG="$ROOT_DIR/artifacts/vendor_dlkm/vendor_dlkm-pixelos-onyx.img"
WIFI_KO=""
PADDING_MIB=8
SPARSE_OUT=""
ALLOW_VERMAGIC_MISMATCH=0
ALLOW_MODULE_NAME_MISMATCH=0
KEEP_STAGE=0

usage() {
	cat <<'EOF'
Usage: tools/make_pixelos_vendor_dlkm_img.sh [options]

Build a PixelOS onyx vendor_dlkm.img from the extracted PixelOS module set.

Options:
  --module-dir DIR                 Source directory with PixelOS vendor_dlkm .ko files.
  --wifi-ko FILE                   Replace qca_cld3_wcn7750.ko with this module.
  --out FILE                       Output image path.
  --padding-mib N                  Free space to add to the ext4 image (default: 8).
  --sparse-out FILE                Also write an Android sparse image for fastboot.
  --allow-vermagic-mismatch        Permit replacement Wi-Fi module vermagic mismatch.
  --allow-module-name-mismatch     Permit replacement Wi-Fi module name mismatch.
  --keep-stage                     Keep the temporary staging directory.
  -h, --help                       Show this help.

Notes:
  The image is ext4 because the local workspace has mke2fs but not mkfs.erofs.
  PixelOS onyx fstab has both ext4 and erofs vendor_dlkm entries.
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--module-dir)
			MODULE_DIR="$2"
			shift 2
			;;
		--wifi-ko)
			WIFI_KO="$2"
			shift 2
			;;
		--out)
			OUT_IMG="$2"
			shift 2
			;;
		--padding-mib)
			PADDING_MIB="$2"
			shift 2
			;;
		--sparse-out)
			SPARSE_OUT="$2"
			shift 2
			;;
		--allow-vermagic-mismatch)
			ALLOW_VERMAGIC_MISMATCH=1
			shift
			;;
		--allow-module-name-mismatch)
			ALLOW_MODULE_NAME_MISMATCH=1
			shift
			;;
		--keep-stage)
			KEEP_STAGE=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown option: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

need() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required tool: $1" >&2
		exit 1
	fi
}

need mke2fs
need tune2fs
need modinfo
need fakeroot
if [[ -n "$SPARSE_OUT" ]]; then
	need img2simg
fi

if ! [[ "$PADDING_MIB" =~ ^[0-9]+$ ]]; then
	echo "--padding-mib must be a non-negative integer: $PADDING_MIB" >&2
	exit 2
fi

if [[ ! -d "$MODULE_DIR" ]]; then
	echo "Module directory not found: $MODULE_DIR" >&2
	exit 1
fi

STOCK_WIFI="$MODULE_DIR/qca_cld3_wcn7750.ko"
if [[ ! -f "$STOCK_WIFI" ]]; then
	echo "Stock PixelOS Wi-Fi module not found: $STOCK_WIFI" >&2
	exit 1
fi

if [[ ! -f "$MODULE_DIR/modules.load" ]]; then
	echo "modules.load not found in $MODULE_DIR" >&2
	exit 1
fi

if [[ -n "$WIFI_KO" ]]; then
	if [[ ! -f "$WIFI_KO" ]]; then
		echo "Replacement Wi-Fi module not found: $WIFI_KO" >&2
		exit 1
	fi

	replacement_name="$(modinfo -F name "$WIFI_KO")"
	if [[ "$replacement_name" != "qca_cld3_wcn7750" && "$ALLOW_MODULE_NAME_MISMATCH" -ne 1 ]]; then
		echo "Replacement module name is '$replacement_name', expected 'qca_cld3_wcn7750'." >&2
		echo "Use --allow-module-name-mismatch only for throwaway testing." >&2
		exit 1
	fi

	stock_vermagic="$(modinfo -F vermagic "$STOCK_WIFI")"
	replacement_vermagic="$(modinfo -F vermagic "$WIFI_KO")"
	if [[ "$stock_vermagic" != "$replacement_vermagic" && "$ALLOW_VERMAGIC_MISMATCH" -ne 1 ]]; then
		echo "Replacement vermagic mismatch:" >&2
		echo "  stock:       $stock_vermagic" >&2
		echo "  replacement: $replacement_vermagic" >&2
		echo "Use --allow-vermagic-mismatch only if the target kernel/module loader is known to accept it." >&2
		exit 1
	fi
fi

mkdir -p "$(dirname "$OUT_IMG")"
STAGE="$(mktemp -d)"
cleanup() {
	if [[ "$KEEP_STAGE" -eq 1 ]]; then
		echo "Keeping stage: $STAGE"
	else
		rm -rf "$STAGE"
	fi
}
trap cleanup EXIT

ROOT="$STAGE/vendor_dlkm"
MOD_DST="$ROOT/lib/modules"
mkdir -p "$MOD_DST"

find "$MODULE_DIR" -maxdepth 1 -type f \( -name '*.ko' -o -name 'modules.load*' -o -name 'modules.blocklist' \) \
	-exec cp -a {} "$MOD_DST/" \;

if [[ -n "$WIFI_KO" ]]; then
	cp -a "$WIFI_KO" "$MOD_DST/qca_cld3_wcn7750.ko"
fi

find "$ROOT" -type d -exec chmod 0755 {} +
find "$ROOT" -type f -exec chmod 0644 {} +

module_count="$(find "$MOD_DST" -maxdepth 1 -type f -name '*.ko' | wc -l)"
if [[ "$module_count" -lt 300 ]]; then
	echo "Refusing to build: only $module_count modules staged; expected the full PixelOS vendor_dlkm set." >&2
	exit 1
fi

used_bytes="$(du -sb "$ROOT" | awk '{print $1}')"
image_bytes=$((used_bytes + PADDING_MIB * 1024 * 1024))
block_size=4096
blocks=$(((image_bytes + block_size - 1) / block_size))

rm -f "$OUT_IMG"
if [[ -n "$SPARSE_OUT" ]]; then
	mkdir -p "$(dirname "$SPARSE_OUT")"
	rm -f "$SPARSE_OUT"
fi
echo "Building $OUT_IMG"
echo "  source modules: $MODULE_DIR"
echo "  staged modules: $module_count"
if [[ -n "$WIFI_KO" ]]; then
	echo "  replacement Wi-Fi: $WIFI_KO"
fi
echo "  padding MiB: $PADDING_MIB"
echo "  ext4 blocks: $blocks"

fakeroot -- bash -c "
	set -e
	chown -R 0:0 '$ROOT'
	mke2fs -q -t ext4 -b $block_size -L vendor_dlkm -d '$ROOT' '$OUT_IMG' $blocks
"
tune2fs -c 0 -i 0 "$OUT_IMG" >/dev/null

echo "Created: $OUT_IMG"
ls -lh "$OUT_IMG"

if [[ -n "$SPARSE_OUT" ]]; then
	img2simg "$OUT_IMG" "$SPARSE_OUT"
	echo "Created sparse image: $SPARSE_OUT"
	ls -lh "$SPARSE_OUT"
fi
