#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STOCK_IMAGE=""
STOCK_ROOT=""
WIFI_KO=""
MODULE_KOS=()
OUT_IMG="$ROOT_DIR/artifacts/vendor_dlkm/vendor_dlkm-pixelos-onyx-qca-injection-erofs.img"
SPARSE_OUT=""
AVBTOOL="${AVBTOOL:-avbtool}"
CLUSTER_SIZE=16384
ALLOW_VERMAGIC_MISMATCH=0
ALLOW_MODULE_NAME_MISMATCH=0
KEEP_STAGE=0

usage() {
	cat <<'EOF'
Usage: tools/make_pixelos_vendor_dlkm_img.sh [options]

Replace the WCN7750 module in a matching PixelOS onyx vendor_dlkm image.

Options:
  --stock-image FILE               Matching stock PixelOS vendor_dlkm image.
  --stock-root DIR                 Optional extracted vendor_dlkm module tree.
  --wifi-ko FILE                   Replacement qca_cld3_wcn7750.ko module.
  --module-ko FILE                 Replacement module named by modinfo, may repeat.
  --out FILE                       Output raw EROFS image.
  --sparse-out FILE                Also write an Android sparse image.
  --avbtool FILE                   avbtool executable/script (default: avbtool).
  --cluster-size BYTES             EROFS compressed cluster size (default: 16384).
  --allow-vermagic-mismatch        Permit replacement Wi-Fi module vermagic mismatch.
  --allow-module-name-mismatch     Permit replacement Wi-Fi module name mismatch.
  --keep-stage                     Keep the temporary staging directory.
  -h, --help                       Show this help.

The output keeps the stock partition size, filesystem contents, ownership, modes,
and SELinux labels. A fresh AVB hashtree/footer is generated without FEC.
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--stock-image)
			STOCK_IMAGE="$2"
			shift 2
			;;
		--stock-root)
			STOCK_ROOT="$2"
			shift 2
			;;
		--wifi-ko)
			WIFI_KO="$2"
			shift 2
			;;
		--module-ko)
			MODULE_KOS+=("$2")
			shift 2
			;;
		--out)
			OUT_IMG="$2"
			shift 2
			;;
		--sparse-out)
			SPARSE_OUT="$2"
			shift 2
			;;
		--avbtool)
			AVBTOOL="$2"
			shift 2
			;;
		--cluster-size)
			CLUSTER_SIZE="$2"
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

run_avbtool() {
	if [[ "$AVBTOOL" == */* ]]; then
		if [[ -x "$AVBTOOL" ]]; then
			"$AVBTOOL" "$@"
		else
			python3 "$AVBTOOL" "$@"
		fi
	else
		"$AVBTOOL" "$@"
	fi
}

need dump.erofs
need fsck.erofs
need mkfs.erofs
need modinfo
need python3
if [[ -n "$SPARSE_OUT" ]]; then
	need img2simg
fi

if [[ -z "$STOCK_IMAGE" || ! -f "$STOCK_IMAGE" ]]; then
	echo "A matching --stock-image is required: $STOCK_IMAGE" >&2
	exit 1
fi
if [[ -n "$STOCK_ROOT" && ! -d "$STOCK_ROOT" ]]; then
	echo "The --stock-root directory does not exist: $STOCK_ROOT" >&2
	exit 1
fi
if [[ -z "$WIFI_KO" || ! -f "$WIFI_KO" ]]; then
	echo "A replacement --wifi-ko is required: $WIFI_KO" >&2
	exit 1
fi
if [[ "$AVBTOOL" == */* && ! -f "$AVBTOOL" ]]; then
	echo "avbtool not found: $AVBTOOL" >&2
	exit 1
fi
if ! [[ "$CLUSTER_SIZE" =~ ^[0-9]+$ ]] || \
	((CLUSTER_SIZE < 4096 || (CLUSTER_SIZE & (CLUSTER_SIZE - 1)) != 0)); then
	echo "--cluster-size must be a power of two of at least 4096 bytes." >&2
	exit 2
fi

partition_size="$(stat -c %s "$STOCK_IMAGE")"
if ((partition_size % 4096 != 0)); then
	echo "Stock image size is not block-aligned: $partition_size" >&2
	exit 1
fi
if ! run_avbtool info_image --image "$STOCK_IMAGE" | grep -q 'Partition Name:[[:space:]]*vendor_dlkm'; then
	echo "Stock image does not contain a vendor_dlkm AVB descriptor." >&2
	exit 1
fi
stock_avb_info="$(run_avbtool info_image --image "$STOCK_IMAGE")"

replacement_name="$(modinfo -F name "$WIFI_KO")"
if [[ "$replacement_name" != "qca_cld3_wcn7750" && "$ALLOW_MODULE_NAME_MISMATCH" -ne 1 ]]; then
	echo "Replacement module name is '$replacement_name', expected 'qca_cld3_wcn7750'." >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT_IMG")"
if [[ -n "$SPARSE_OUT" ]]; then
	mkdir -p "$(dirname "$SPARSE_OUT")"
fi

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
CONTEXTS="$STAGE/file_contexts"
FS_IMAGE="$STAGE/vendor_dlkm.erofs"
mkdir -p "$ROOT"

if [[ -n "$STOCK_ROOT" ]]; then
	echo "Staging extracted stock vendor_dlkm modules"
	if [[ -f "$STOCK_ROOT/lib/modules/qca_cld3_wcn7750.ko" ]]; then
		cp -a "$STOCK_ROOT/." "$ROOT/"
	elif [[ -f "$STOCK_ROOT/qca_cld3_wcn7750.ko" ]]; then
		mkdir -p "$ROOT/lib/modules"
		cp -a "$STOCK_ROOT/." "$ROOT/lib/modules/"
	else
		echo "Stock Wi-Fi module not found in --stock-root: $STOCK_ROOT" >&2
		exit 1
	fi
else
	fsck_help="$(fsck.erofs --help 2>&1 || true)"
	if ! grep -Eq -- '--extract(\[|=)' <<<"$fsck_help"; then
		echo "This fsck.erofs cannot extract to a directory." >&2
		echo "Use --stock-root with PixelOS modules/vendor_dlkm, or install newer erofs-utils." >&2
		exit 1
	fi
	FSCK_EXTRACT_ARGS=(--extract="$ROOT")
	if grep -Fq -- '--[no-]preserve' <<<"$fsck_help"; then
		FSCK_EXTRACT_ARGS+=(--no-preserve)
	fi
	echo "Extracting stock EROFS image"
	fsck.erofs "${FSCK_EXTRACT_ARGS[@]}" "$STOCK_IMAGE" >/dev/null
fi

STOCK_WIFI="$ROOT/lib/modules/qca_cld3_wcn7750.ko"
if [[ ! -f "$STOCK_WIFI" ]]; then
	echo "Stock Wi-Fi module not found in image: $STOCK_WIFI" >&2
	exit 1
fi

stock_vermagic="$(modinfo -F vermagic "$STOCK_WIFI")"
replacement_vermagic="$(modinfo -F vermagic "$WIFI_KO")"
if [[ "$stock_vermagic" != "$replacement_vermagic" && "$ALLOW_VERMAGIC_MISMATCH" -ne 1 ]]; then
	echo "Replacement vermagic mismatch:" >&2
	echo "  stock:       $stock_vermagic" >&2
	echo "  replacement: $replacement_vermagic" >&2
	echo "Use --allow-vermagic-mismatch only with its matching custom kernel." >&2
	exit 1
fi

install -m 0644 "$WIFI_KO" "$STOCK_WIFI"
for module_ko in "${MODULE_KOS[@]}"; do
	if [[ ! -f "$module_ko" ]]; then
		echo "Replacement module not found: $module_ko" >&2
		exit 1
	fi
	module_name="$(modinfo -F name "$module_ko")"
	if [[ -z "$module_name" ]]; then
		echo "Could not read module name from: $module_ko" >&2
		exit 1
	fi
	stock_module="$ROOT/lib/modules/$module_name.ko"
	if [[ ! -f "$stock_module" ]]; then
		echo "Stock module not found in image: $stock_module" >&2
		exit 1
	fi
	module_vermagic="$(modinfo -F vermagic "$module_ko")"
	if [[ "$stock_vermagic" != "$module_vermagic" && "$ALLOW_VERMAGIC_MISMATCH" -ne 1 ]]; then
		echo "Replacement vermagic mismatch for $module_name:" >&2
		echo "  stock Wi-Fi:  $stock_vermagic" >&2
		echo "  replacement: $module_vermagic" >&2
		exit 1
	fi
	install -m 0644 "$module_ko" "$stock_module"
done
module_count="$(find "$ROOT/lib/modules" -maxdepth 1 -type f -name '*.ko' | wc -l)"
if [[ "$module_count" -lt 300 ]]; then
	echo "Refusing to build: only $module_count modules were extracted." >&2
	exit 1
fi

filesystem_uuid="$(dump.erofs -s "$STOCK_IMAGE" | awk -F': *' '/Filesystem UUID:/ {print $2; exit}')"
if [[ -z "$filesystem_uuid" ]]; then
	echo "Could not read the stock EROFS UUID." >&2
	exit 1
fi
fec_num_roots="$(awk -F': *' '/FEC num roots:/ {print $2; exit}' <<<"$stock_avb_info")"
AVB_FEC_ARGS=()
if [[ "$fec_num_roots" =~ ^[0-9]+$ ]] && ((fec_num_roots > 0)) && \
	command -v fec >/dev/null 2>&1; then
	AVB_FEC_ARGS=(--fec_num_roots "$fec_num_roots")
else
	AVB_FEC_ARGS=(--do_not_generate_fec)
fi
AVB_PROP_ARGS=()
while IFS= read -r line; do
	prop="$(sed -n "s/^[[:space:]]*Prop: \\(.*\\) -> '\\(.*\\)'$/\\1:\\2/p" <<<"$line")"
	if [[ -n "$prop" ]]; then
		AVB_PROP_ARGS+=(--prop "$prop")
	fi
done <<<"$stock_avb_info"

# PixelOS labels vendor_dlkm contents as vendor_file. Extraction as an ordinary
# CI user cannot restore security.selinux xattrs, so mkfs applies them directly.
{
	printf '%s\n' '/(/.*)? u:object_r:vendor_file:s0'
	printf '%s\n' '/vendor_dlkm(/.*)? u:object_r:vendor_file:s0'
} >"$CONTEXTS"

echo "Building replacement EROFS image"
echo "  stock image:       $STOCK_IMAGE"
if [[ -n "$STOCK_ROOT" ]]; then
	echo "  stock root:        $STOCK_ROOT"
fi
echo "  partition bytes:   $partition_size"
echo "  replacement Wi-Fi: $WIFI_KO"
for module_ko in "${MODULE_KOS[@]}"; do
	echo "  replacement mod:  $module_ko"
done
echo "  replacement magic: $replacement_vermagic"
echo "  EROFS cluster:     $CLUSTER_SIZE"
if [[ "${AVB_FEC_ARGS[*]}" == *--do_not_generate_fec* ]]; then
	echo "  AVB FEC roots:     0"
else
	echo "  AVB FEC roots:     ${fec_num_roots:-0}"
fi

MKFS_ARGS=(
	--quiet
	-zlz4hc,level=12
	-C"$CLUSTER_SIZE"
	-T1230768000
	--all-root
	--file-contexts="$CONTEXTS"
	-U"$filesystem_uuid"
)
if mkfs.erofs --help 2>&1 | grep -q -- '--mount-point'; then
	MKFS_ARGS+=(--mount-point=/vendor_dlkm)
fi
mkfs.erofs "${MKFS_ARGS[@]}" "$FS_IMAGE" "$ROOT"

rm -f "$OUT_IMG" "$SPARSE_OUT"
cp "$FS_IMAGE" "$OUT_IMG"
run_avbtool add_hashtree_footer \
	--image "$OUT_IMG" \
	--partition_name vendor_dlkm \
	--partition_size "$partition_size" \
	--hash_algorithm sha256 \
	"${AVB_FEC_ARGS[@]}" \
	"${AVB_PROP_ARGS[@]}"

if [[ "$(stat -c %s "$OUT_IMG")" -ne "$partition_size" ]]; then
	echo "Output size does not match the stock partition image." >&2
	exit 1
fi

echo "Created: $OUT_IMG"
ls -lh "$OUT_IMG"

if [[ -n "$SPARSE_OUT" ]]; then
	img2simg "$OUT_IMG" "$SPARSE_OUT"
	echo "Created sparse image: $SPARSE_OUT"
	ls -lh "$SPARSE_OUT"
fi
