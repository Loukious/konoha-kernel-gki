#!/system/bin/sh
# KernelSU post-fs-data hook: archive, never unlink, previous-boot records.
# Runs on the restored kernel; recovery needs its own collector before reboot.
umask 077
base=/data/adb/onyx-boot-logs
boot_id=$(cat /proc/sys/kernel/random/boot_id) || exit 1
dest=$base/$boot_id
mkdir -p "$dest/pstore" || exit 1
exec 2>>"$dest/capture-errors.txt"
# Do not overwrite evidence if invoked again during this boot.
if [ ! -f "$dest/complete" ]; then
    for record in /sys/fs/pstore/*; do
        [ -f "$record" ] || continue
        cp -p "$record" "$dest/pstore/"
    done
    cat /proc/cmdline >"$dest/cmdline.txt"
    uname -a >"$dest/kernel.txt"
    dmesg >"$dest/current-dmesg.txt"
    getprop ro.boot.bootreason >"$dest/bootreason.txt"
    for parameter in /sys/module/ramoops/parameters/*; do
        [ -f "$parameter" ] || continue
        echo "$parameter"
        cat "$parameter"
    done >"$dest/ramoops-parameters.txt"
    ls -la /sys/fs/pstore >"$dest/pstore-list.txt"
    date -u >"$dest/complete"
    sync
fi
