#!/usr/bin/env bash
set -euo pipefail

# Build a recovery-flashable zip around a vendor_dlkm image.
#
# vendor_dlkm is EROFS (read-only, no write support in any recovery), so the
# only way to swap qca_cld3_wcn7750.ko without fastboot is to rewrite the whole
# partition with a repacked image. The installer follows the Magisk shell
# update-binary pattern: extract a bundled arm64 busybox with the recovery's
# own unzip, then dd the image to the active slot's vendor_dlkm block device
# with md5 readback verification. Verified working on TWRP/OrangeFox (onyx).
#
# Usage:
#   tools/make_vendor_dlkm_flash_zip.sh <vendor_dlkm.img> <arm64-busybox> <out.zip> [kernel-release]

IMG="${1:?usage: make_vendor_dlkm_flash_zip.sh <vendor_dlkm.img> <busybox> <out.zip> [kernel-release]}"
BB="${2:?missing arm64 busybox path}"
OUT_ZIP="${3:?missing output zip path}"
KERNEL_RELEASE="${4:-}"

for tool in md5sum stat zip; do
	command -v "$tool" >/dev/null 2>&1 || { echo "Missing required tool: $tool" >&2; exit 1; }
done
[[ -f "$IMG" ]] || { echo "vendor_dlkm image not found: $IMG" >&2; exit 1; }
[[ -f "$BB" ]] || { echo "arm64 busybox not found: $BB" >&2; exit 1; }

img_md5="$(md5sum "$IMG" | awk '{print $1}')"
img_size="$(stat -c %s "$IMG")"
[[ -n "$KERNEL_RELEASE" ]] && release_note="pairs with the kernel release: $KERNEL_RELEASE" \
	|| release_note="pairs with the Kono-Ha kernel zip from the same release"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/META-INF/com/google/android" "$STAGE/lib/arm64-v8a"

cat >"$STAGE/META-INF/com/google/android/update-binary" <<'UPDATE_BINARY'
#!/sbin/sh
#
# Shell update-binary (the Magisk pattern): use the recovery's own unzip to
# extract a bundled busybox, then run updater-script as a shell script.
# This works on TWRP, OrangeFox and any recovery that can install Magisk.
#
# $1 = output fd, $2 = command, $3 = zip path
#
TMPDIR=/dev/tmp
rm -rf $TMPDIR
mkdir -p $TMPDIR 2>/dev/null

export BBBIN=$TMPDIR/busybox
for arch in arm64-v8a armeabi-v7a; do
  unzip -o "$3" "lib/$arch/libbusybox.so" -d $TMPDIR >&2
  libpath="$TMPDIR/lib/$arch/libbusybox.so"
  chmod 755 $libpath
  if [ -x $libpath ] && $libpath >/dev/null 2>&1; then
    mv -f $libpath $BBBIN
    break
  fi
done
[ -x $BBBIN ] || { echo "FATAL: no usable busybox (recovery unzip missing?)" >&2; exit 1; }

export INSTALLER=$TMPDIR/install
$BBBIN mkdir -p $INSTALLER
$BBBIN unzip -o "$3" "META-INF/com/google/android/updater-script" "vendor_dlkm.img" -d $INSTALLER >&2
[ -f "$INSTALLER/vendor_dlkm.img" ] || { echo "FATAL: vendor_dlkm.img not extracted" >&2; exit 1; }

export ASH_STANDALONE=1
exec $BBBIN sh "$INSTALLER/META-INF/com/google/android/updater-script" "$@"
UPDATE_BINARY

cat >"$STAGE/META-INF/com/google/android/updater-script" <<'UPDATER_SCRIPT'
#!/sbin/sh
#
# onyx vendor_dlkm re-flash: Kono-Ha nethunter WLAN package.
#
# Why a whole-partition dd and not an in-place .ko copy: vendor_dlkm is
# EROFS — read-only by design, with no write support in any recovery. The
# only way to swap qca_cld3_wcn7750.ko without fastboot is to rewrite the
# partition with an image that already contains the replacement module.
#
# Flash target: the ACTIVE slot's vendor_dlkm block device. On dynamic
# partitions the by-name node is a dm-linear mapping over super created by
# the recovery; writing through it lands at the right offset inside super,
# which is precisely how recoveries flash images to logical partitions.
#
# This image @RELEASE_NOTE@.
# Flash the matching Kono-Ha kernel zip BEFORE (or right after) this one;
# the WLAN module inside is built for that kernel's ABI.

OUTFD="$1"
ZIPFILE="$3"
BB="$BBBIN"

ui_print() {
  echo -n -e "ui_print $1\n" >> /proc/self/fd/"$OUTFD"
  echo -n -e "ui_print\n"     >> /proc/self/fd/"$OUTFD"
}

IMG="$INSTALLER/vendor_dlkm.img"
WANT_IMG_MD5=@IMG_MD5@

ui_print " "
ui_print "==============================="
ui_print " onyx vendor_dlkm (WLAN module)"
ui_print "==============================="
ui_print " "
ui_print " image built for kernel: @KERNEL_RELEASE@"
RUNNING_UREL=$($BB uname -r 2>/dev/null)
[ -n "$RUNNING_UREL" ] && ui_print " currently running kernel: $RUNNING_UREL"

# --- 1. verify the payload before touching anything -----------------------
got_img_md5=$($BB md5sum "$IMG" | cut -d' ' -f1)
if [ "$got_img_md5" != "$WANT_IMG_MD5" ]; then
  ui_print "E10: image md5 mismatch ($got_img_md5)"
  ui_print "     refusing to flash a corrupt download"
  exit 1
fi
ui_print " image md5 verified: $got_img_md5"
IMG_SIZE=$($BB stat -c %s "$IMG")
ui_print " image size: $IMG_SIZE bytes"

# --- 2. resolve the active slot's vendor_dlkm block device ----------------
GPROP=""
for g in /system/bin/getprop /sbin/getprop getprop; do
  command -v "$g" >/dev/null 2>&1 && { GPROP="$g"; break; }
done
SLOT=""
[ -z "$GPROP" ] || SLOT=$("$GPROP" ro.boot.slot_suffix 2>/dev/null)
[ -n "$SLOT" ] || SLOT=$($BB grep -o 'androidboot.slot_suffix=._' /proc/cmdline 2>/dev/null | $BB tail -c 2)
ui_print " active slot: ${SLOT:-unknown}"

DEV=""
for d in "/dev/block/by-name/vendor_dlkm$SLOT" \
         "/dev/block/mapper/vendor_dlkm$SLOT" \
         "/dev/block/by-name/vendor_dlkm" \
         "/dev/block/mapper/vendor_dlkm"; do
  [ -b "$d" ] && { DEV="$d"; break; }
done
if [ -z "$DEV" ]; then
  ui_print "E20: no vendor_dlkm block device found"
  ui_print "     (recovery did not map the dynamic partition)"
  ui_print "     please flash vendor_dlkm.img via fastboot instead"
  exit 1
fi
ui_print " target: $DEV"

# --- 3. sanity: big enough, and currently holds an erofs filesystem --------
DEV_SIZE=""
DEV_SIZE=$($BB blockdev --getsize64 "$DEV" 2>/dev/null) || DEV_SIZE=""
if [ -z "$DEV_SIZE" ] || [ "$DEV_SIZE" -lt "$IMG_SIZE" ]; then
  ui_print "E30: target too small or size unknown (${DEV_SIZE:-?} < $IMG_SIZE)"
  ui_print "     refusing to flash — wrong partition?"
  exit 1
fi
ui_print " partition size: $DEV_SIZE bytes"
MAGIC=$($BB dd if="$DEV" bs=1 skip=1024 count=4 2>/dev/null | $BB od -An -tx1 | $BB tr -d ' \n')
if [ "$MAGIC" != "e2e1f5e0" ]; then
  ui_print "E31: target is not an EROFS image (magic: $MAGIC)"
  ui_print "     refusing to flash — wrong partition?"
  exit 1
fi
ui_print " target currently holds an EROFS image: OK"

# --- 4. flash, then read back and verify -----------------------------------
ui_print " "
ui_print " flashing..."
$BB dd if="$IMG" of="$DEV" bs=4M conv=fsync 2>/dev/null
sync
ui_print " verifying..."
got_back=$($BB head -c "$IMG_SIZE" "$DEV" | $BB md5sum | cut -d' ' -f1)
if [ "$got_back" != "$WANT_IMG_MD5" ]; then
  ui_print "E40: readback md5 mismatch ($got_back)"
  ui_print "     DO NOT REBOOT if you can re-run this zip;"
  ui_print "     otherwise reflash vendor_dlkm via fastboot"
  exit 1
fi
ui_print " readback md5 verified"

ui_print " "
ui_print " done — reboot and check wlan0"
ui_print "==============================="
ui_print " "
exit 0
UPDATER_SCRIPT

sed -i \
	-e "s/@IMG_MD5@/$img_md5/" \
	-e "s/@RELEASE_NOTE@/$release_note/" \
	-e "s/@KERNEL_RELEASE@/${KERNEL_RELEASE:-see release notes}/" \
	"$STAGE/META-INF/com/google/android/updater-script"

cp "$IMG" "$STAGE/vendor_dlkm.img"
cp "$BB" "$STAGE/lib/arm64-v8a/libbusybox.so"

mkdir -p "$(dirname "$OUT_ZIP")"
OUT_ZIP="$(cd "$(dirname "$OUT_ZIP")" && printf '%s/%s' "$PWD" "$(basename "$OUT_ZIP")")"
rm -f "$OUT_ZIP"
( cd "$STAGE" && zip -qr9 "$OUT_ZIP" \
	META-INF/com/google/android/update-binary \
	META-INF/com/google/android/updater-script \
	lib/arm64-v8a/libbusybox.so \
	vendor_dlkm.img )

echo "[+] Created: $OUT_ZIP"
echo "    image md5:   $img_md5"
echo "    image bytes: $img_size"
ls -lh "$OUT_ZIP"
