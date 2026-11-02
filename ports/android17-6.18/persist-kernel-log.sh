#!/system/bin/sh
# Diagnostic capture for the first 120 seconds after invocation.
# Use "recovery" in the rescue ramdisk; KernelSU service.d uses "android".
# Does not clear pstore or kernel ring buffers, or write raw partitions.
umask 077
mode=${1:-android}
if [ "$mode" = recovery ]; then
    # The metadata partition is not FBE credential encrypted. Mount only the
    # named partition, and never format it or alter other mount points.
    n=0
    while [ ! -b /dev/block/by-name/metadata ] && [ "$n" -lt 15 ]; do
        sleep 1
        n=$((n + 1))
    done
    if ! grep -q ' /metadata ' /proc/mounts; then
        mount -t f2fs -o nosuid,nodev /dev/block/by-name/metadata /metadata
    fi
    if grep -q ' /metadata ' /proc/mounts; then
        base=/metadata/onyx-boot-logs
    else
        # Explicit failure marker: these files require ADB before reboot.
        base=/tmp/onyx-boot-logs
    fi
else
    base=/data/adb/onyx-boot-logs
fi
boot_id=$(cat /proc/sys/kernel/random/boot_id) || exit 1
dest=$base/$boot_id
mkdir -p "$dest/pstore" || exit 1
exec 2>>"$dest/persist-errors.txt"
# Preserve the first snapshot. A later invocation must not replace it.
if [ ! -f "$dest/persist-started" ]; then
    for record in /sys/fs/pstore/*; do
        [ -f "$record" ] || continue
        cp -p "$record" "$dest/pstore/"
    done
    cat /proc/cmdline >"$dest/persist-cmdline.txt"
    uname -a >"$dest/persist-kernel.txt"
    echo "$base" >"$dest/storage.txt"
    date -u >"$dest/persist-started"
    sync
else
    exit 0
fi
# Bound time and size, and leave CPU time for boot. This is diagnostic, not
# permanent tracing. The newest second of logs can still be lost on a crash.
timeout 120 dmesg -w | head -c 16777216 >"$dest/kernel-live.txt" &
writer=$!
n=0
while kill -0 "$writer" 2>/dev/null && [ "$n" -lt 120 ]; do
    sync -f "$dest/kernel-live.txt"
    sleep 1
    n=$((n + 1))
done
wait "$writer"
sync
