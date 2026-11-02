### Onyx experimental Android 17 / 6.18 boot-test package
properties() { '
kernel.string=Konoha Onyx 6.18 experimental boot test
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=onyx
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; }

block=boot
is_slot_device=1
slot_select=active
no_magisk_check=1
. tools/ak3-core.sh

is_block_device() { [ -b "$1" ]; }

find_partition() {
  local base candidate
  for base in /dev/block/mapper /dev/block/by-name /dev/block/bootdevice/by-name; do
    candidate="$base/$1$slot"
    if is_block_device "$candidate"; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

check_environment() {
  local device unlocked vbmeta magic flags
  case "$slot" in _a|_b) ;; *) abort "Cannot determine the active A/B slot.";; esac
  device="$(getprop ro.product.device) $(getprop ro.vendor.product.device) $(getprop ro.product.vendor.device)"
  case " $device " in *" onyx "*) ;; *) abort "This package is for Onyx only.";; esac
  if ps -A 2>/dev/null | grep -q '[z]ygote'; then
    abort "Install this full boot-test package from recovery."
  fi
  unlocked="$(getprop ro.boot.flash.locked):$(getprop ro.boot.vbmeta.device_state):$(getprop ro.boot.verifiedbootstate)"
  case "$unlocked" in 0:*|*:unlocked:*|*:*:orange) ;; *) abort "An unlocked bootloader is required.";; esac
  vbmeta=$(find_partition vbmeta) || abort "Active-slot vbmeta partition is missing."
  magic=$(dd if="$vbmeta" bs=1 count=4 2>/dev/null)
  [ "$magic" = AVB0 ] || abort "Cannot read the active vbmeta header."
  flags=$(dd if="$vbmeta" bs=1 skip=120 count=4 2>/dev/null | od -An -tu1)
  set -- $flags
  [ "$#" -eq 4 ] || abort "Cannot read vbmeta verification flags."
  flags=$(( ($1 << 24) | ($2 << 16) | ($3 << 8) | $4 ))
  [ $((flags & 3)) -eq 3 ] || abort "This test package requires AVB and verity already disabled, as in the baseline ROM."
  for device in busybox httools_static lptools_static snapshotupdater_static; do
    [ -x "$bin/$device" ] || abort "Required recovery tool is missing: $device"
  done
  sha256sum -c "$home/tools.sha256" >/dev/null 2>&1 || abort "Installer tool checksum verification failed."
}

preflight_payloads() {
  local part bytes expected extra image actual target capacity seen count resizes
  seen=" "
  count=0
  resizes=0
  : > "$home/install-plan.tsv"
  while read -r part bytes expected extra; do
    [ -n "$part" ] || continue
    case "$part" in boot|vendor_boot|dtbo|vendor_dlkm|system_dlkm) ;; *) abort "Unknown image in payload manifest.";; esac
    [ -z "$extra" ] || abort "Invalid payload manifest fields."
    case "$seen" in *" $part "*) abort "Duplicate payload: $part";; esac
    seen="$seen$part "
    case "$bytes" in ''|*[!0-9]*) abort "Invalid image size: $part";; esac
    case "$expected" in ''|*[!0-9a-f]*) abort "Invalid image checksum: $part";; esac
    [ "${#expected}" -eq 64 ] || abort "Invalid checksum length: $part"
    image="$home/$part.img"
    [ ! -e "$home/${part}_flashed" ] || abort "Stale install marker: restart installation from the ZIP."
    [ -f "$image" ] || abort "Missing image: $part"
    actual=$(wc -c < "$image" | tr -d '[:space:]')
    [ "$actual" -eq "$bytes" ] || abort "Image size mismatch: $part"
    actual=$(sha256sum "$image" | cut -d' ' -f1)
    [ "$actual" = "$expected" ] || abort "Image checksum mismatch: $part"
    target=$(find_partition "$part") || abort "Active-slot partition is missing: $part"
    capacity=$(blockdev --getsize64 "$target" 2>/dev/null) || abort "Cannot read partition size: $part"
    case "$capacity" in ''|*[!0-9]*) abort "Invalid partition size: $part";; esac
    case "$part" in
      vendor_dlkm|system_dlkm)
        [ ! -e "/dev/block/mapper/${part}_ak3" ] || abort "A previous $part resize mapping exists; reboot recovery first."
        if [ "$capacity" -ne "$bytes" ]; then
          resizes=$((resizes + 1))
          ui_print "$part will be resized from $capacity to $bytes bytes."
        fi
        ;;
      *) [ "$capacity" -ge "$bytes" ] || abort "Image is larger than the physical partition: $part";;
    esac
    printf '%s\t%s\t%s\t%s\n' "$part" "$target" "$bytes" "$expected" >> "$home/install-plan.tsv"
    count=$((count + 1))
  done < "$home/payloads.tsv"
  [ "$count" -eq 5 ] || abort "The package must contain all five matched images."
  [ "$resizes" -le 1 ] || abort "This recovery helper requires a reboot between multiple logical resizes; use the baseline ROM partition layout."
}

install_matched_images() {
  local part line target bytes expected actual
  # Logical allocation/resize happens first. The new kernel is written last.
  for part in system_dlkm vendor_dlkm dtbo vendor_boot boot; do
    line=$(grep "^$part[[:space:]]" "$home/install-plan.tsv")
    set -- $line
    [ "$#" -eq 4 ] || abort "Invalid preflight plan for $part"
    target=$2
    bytes=$3
    expected=$4
    [ "$(find_partition "$part")" = "$target" ] || abort "Partition mapping changed after preflight: $part"
    actual=$(sha256sum "$home/$part.img" | cut -d' ' -f1)
    [ "$actual" = "$expected" ] || abort "Payload changed after preflight: $part"
    ak3_last_flashed_block=
    ui_print "Installing and verifying $part on $slot..."
    flash_generic "$part" || abort "Flashing $part failed. Restore matching ROM images before rebooting."
    is_block_device "$ak3_last_flashed_block" || abort "Missing $part readback target."
    actual=$(head -c "$bytes" "$ak3_last_flashed_block" | sha256sum | cut -d' ' -f1)
    [ "$actual" = "$expected" ] || abort "$part readback failed. Restore matching ROM images before rebooting."
  done
  sync
  ui_print "Matched 6.18 boot-test images installed and read back successfully."
  ui_print "Experimental: hardware is unvalidated; optional drivers and KernelSU/NetHunter completion are deferred."
}

ui_print "Onyx experimental 6.18 boot test: checking every payload before writing."
check_environment
preflight_payloads
install_matched_images
