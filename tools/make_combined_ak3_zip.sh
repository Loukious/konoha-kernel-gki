#!/usr/bin/env bash
set -euo pipefail

# Turn an AnyKernel3 kernel zip into a combined kernel + vendor_dlkm flasher.
#
# vendor_dlkm is EROFS (read-only, no write support in any recovery), so the
# only way to swap qca_cld3_wcn7750.ko without fastboot is to rewrite the whole
# partition with a repacked image. This appends a vendor_dlkm install section
# to anykernel.sh — it runs AFTER the kernel is written, using AK3's own
# busybox, ui_print/abort helpers, active-slot detection ($slot, leading
# underscore) and extraction dir ($AKHOME) — and drops the image at the zip
# root so update-binary unzips it to $AKHOME/vendor_dlkm.img.
#
# Verified layout on-device (onyx, TWRP/OrangeFox): same as the hand-built
# onyx-kono-ha-kernel-wlan-fix-20260829.zip.
#
# Usage:
#   tools/make_combined_ak3_zip.sh <ak3-kernel.zip> <vendor_dlkm.img> <kernel-release> <out.zip>

AK3_ZIP="${1:?usage: make_combined_ak3_zip.sh <ak3.zip> <vendor_dlkm.img> <kernel-release> <out.zip>}"
IMG="${2:?missing vendor_dlkm.img}"
KERNEL_RELEASE="${3:?missing kernel release}"
OUT_ZIP="${4:?missing output zip path}"

for tool in md5sum stat unzip zip sed; do
	command -v "$tool" >/dev/null 2>&1 || { echo "Missing required tool: $tool" >&2; exit 1; }
done
[[ -f "$AK3_ZIP" ]] || { echo "AK3 kernel zip not found: $AK3_ZIP" >&2; exit 1; }
[[ -f "$IMG" ]] || { echo "vendor_dlkm image not found: $IMG" >&2; exit 1; }

img_md5="$(md5sum "$IMG" | awk '{print $1}')"
img_size="$(stat -c %s "$IMG")"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
unzip -q "$AK3_ZIP" -d "$STAGE"
[[ -f "$STAGE/anykernel.sh" ]] || { echo "anykernel.sh not found inside $AK3_ZIP — not an AnyKernel3 zip?" >&2; exit 1; }

# The resize path needs AK3's logical-partition machinery: flash_generic()
# plus the lptools/snapshotupdater/httools static binaries it drives.
grep -q "flash_generic()" "$STAGE/tools/ak3-core.sh" || { echo "tools/ak3-core.sh has no flash_generic() — AK3 too old for logical-partition resize" >&2; exit 1; }
for tool in lptools_static snapshotupdater_static httools_static busybox; do
	[[ -f "$STAGE/tools/$tool" ]] || { echo "AK3 is missing tools/$tool — cannot resize logical partitions" >&2; exit 1; }
done

# The section must run after AK3 has flashed the kernel: anykernel.sh sources
# tools/ak3-core.sh (which defines ui_print/abort and sets $slot/$AKHOME) and
# then runs its install commands, so appending to the end of the file is
# exactly "after the kernel write".
cat >>"$STAGE/anykernel.sh" <<'VD_BLOCK'

## vendor_dlkm install (konoha-ABI WLAN module — @KERNEL_RELEASE@)
## Appended by tools/make_combined_ak3_zip.sh. Runs after the kernel write;
## uses AK3's own flash_generic() so a size mismatch with the current
## logical partition is handled properly instead of aborting: dm-verity
## detection + AVB patching (httools), and when the image is larger than
## the partition, a Virtual A/B snapshot update or an lptools resize/
## replace inside super. The image may be built from a different ROM's
## vendor_dlkm template than the one installed — that is fine, the
## container just has to land on the device whole.
VDIMG="$AKHOME/vendor_dlkm.img"
VDMD5=@IMG_MD5@

ui_print " "
ui_print "Installing vendor_dlkm (WLAN module)..."
ui_print " image built for kernel: @KERNEL_RELEASE@"

vd_got=$(md5sum "$VDIMG" | cut -d' ' -f1)
[ "$vd_got" = "$VDMD5" ] || abort "vendor_dlkm image md5 mismatch ($vd_got) - aborting"
VD_SIZE=$(stat -c %s "$VDIMG")

flash_generic vendor_dlkm

ui_print " verifying..."
VD_DEV=""
# flash_generic's lptools create/replace path flashes to a NEW mapping named
# ${1}_ak3, so probe that first -- vendor_dlkm$slot may still be the old,
# smaller partition that was replaced.
for vd_d in "/dev/block/mapper/vendor_dlkm_ak3" "/dev/block/mapper/vendor_dlkm$slot" "/dev/block/by-name/vendor_dlkm$slot" "/dev/block/mapper/vendor_dlkm" "/dev/block/by-name/vendor_dlkm"; do
	[ -b "$vd_d" ] && { VD_DEV="$vd_d"; break; }
done
if [ -n "$VD_DEV" ]; then
	VD_PSIZE=$(blockdev --getsize64 "$VD_DEV" 2>/dev/null || wc -c < "$VD_DEV")
	if [ "$VD_PSIZE" -ge "$VD_SIZE" ]; then
		VD_BACK=$(head -c "$VD_SIZE" "$VD_DEV" | md5sum | cut -d' ' -f1)
		[ "$VD_BACK" = "$VDMD5" ] || abort "vendor_dlkm readback md5 mismatch ($VD_BACK) - reflash vendor_dlkm via fastboot before rebooting"
		ui_print " vendor_dlkm verified ($VD_DEV)."
	else
		ui_print " note: $VD_DEV is still $VD_PSIZE bytes; the resized"
		ui_print " mapping appears after reboot - verify Wi-Fi once booted."
	fi
else
	ui_print " note: vendor_dlkm is not mapped after install - readback skipped"
	ui_print " (a resize takes effect after reboot; verify Wi-Fi once booted)"
fi
ui_print " "
## end vendor_dlkm install
VD_BLOCK

sed -i \
	-e "s/@IMG_MD5@/$img_md5/" \
	-e "s/@KERNEL_RELEASE@/$KERNEL_RELEASE/g" \
	"$STAGE/anykernel.sh"

grep -q "$img_md5" "$STAGE/anykernel.sh" || { echo "md5 substitution failed" >&2; exit 1; }

cp "$IMG" "$STAGE/vendor_dlkm.img"

mkdir -p "$(dirname "$OUT_ZIP")"
OUT_ZIP="$(cd "$(dirname "$OUT_ZIP")" && printf '%s/%s' "$PWD" "$(basename "$OUT_ZIP")")"
rm -f "$OUT_ZIP"
( cd "$STAGE" && zip -qr9 "$OUT_ZIP" . )

echo "[+] Created combined kernel + vendor_dlkm zip: $OUT_ZIP"
echo "    kernel release: $KERNEL_RELEASE"
echo "    vendor_dlkm md5:   $img_md5"
echo "    vendor_dlkm bytes: $img_size"
ls -lh "$OUT_ZIP"
