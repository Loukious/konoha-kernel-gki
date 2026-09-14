#!/usr/bin/env bash
set -euo pipefail

# Package extra out-of-tree kernel modules as a KernelSU/Magisk module zip.
#
# The module ships the .ko files under system/lib/modules/<kernel-release>/
# and insmods them from service.sh (late_start). At that point the
# vendor_dlkm module set (cfg80211, mac80211, ...) is already loaded — but
# we still wait for dependencies that are slow to appear, so each module
# lists its deps in MODULE_DEPS and service.sh polls /sys/module/<dep>.
#
# Usage:
#   make_ksu_module_zip.sh --out FILE.zip --module-name NAME --module-id ID \
#     --version V --versionCode N [--description TXT]
#     --ko NAME=path/to/NAME.ko [--dep NAME=dep1,dep2] ...
#
# --ko and --dep take the module name (modinfo -F name) as the key; the
# dep list names modules, not files. Multiple --ko/--dep pairs allowed.
#
# A module given as --swap NAME=path instead of --ko gets "replace the
# already-loaded stock driver" semantics in service.sh: wait for the stock
# module, stop Wi-Fi, wait for its refcount to drain, rmmod, insmod ours,
# restart Wi-Fi. Validated live on onyx 2026-09-11: the swap survives a
# full Wi-Fi reconnect (11ax, same IP) and monitor mode + injection work
# after it. If insmod fails, the stock driver is re-insmodded from
# /vendor_dlkm/lib/modules/ as a best-effort fallback.
#
# --fw DIR ships a firmware directory (e.g. tools/firmware, containing
# rtlwifi/*.bin) inside the module and wires service.sh to make it
# reachable for request_firmware(): the ROM's firmware search path is
# /odm/firmware/o10u (read-only erofs), so service.sh bind-mounts an
# overlay of it (ROM's own files + ours, labeled vendor_file to satisfy
# enforcing SELinux) before any module is loaded. Validated on onyx
# 2026-09-14: RTL8188CU dongle firmware loads and the device probes under
# enforcing SELinux. Modules whose driver requests firmware MUST be
# listed after it in --ko order (service.sh runs fw_setup first).

OUT_ZIP=""
MODULE_NAME=""
MODULE_ID=""
VERSION=""
VERSION_CODE=""
DESCRIPTION=""
FW_DIR=""
declare -A KO_PATHS DEP_LISTS SWAP_MODULES
ORDERED_KOS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--out) OUT_ZIP="$2"; shift 2 ;;
		--module-name) MODULE_NAME="$2"; shift 2 ;;
		--module-id) MODULE_ID="$2"; shift 2 ;;
		--version) VERSION="$2"; shift 2 ;;
		--versionCode) VERSION_CODE="$2"; shift 2 ;;
		--description) DESCRIPTION="$2"; shift 2 ;;
		--fw)
			[[ -d "$2" ]] || { echo "Firmware directory not found: $2" >&2; exit 1; }
			FW_DIR="$2"; shift 2 ;;
		--ko|--swap)
			swap=0
			[[ "$1" == "--swap" ]] && swap=1
			key="${2%%=*}"; path="${2#*=}"
			[[ "$key" != "$2" && -n "$key" && -n "$path" ]] || {
				echo "$1 expects NAME=path/to/NAME.ko" >&2; exit 1; }
			[[ -f "$path" ]] || { echo "Module not found: $path" >&2; exit 1; }
			KO_PATHS["$key"]="$path"
			ORDERED_KOS+=("$key")
			((swap)) && SWAP_MODULES["$key"]=1
			shift 2 ;;
		--dep)
			key="${2%%=*}"; deps="${2#*=}"
			DEP_LISTS["$key"]="$deps"
			shift 2 ;;
		*)
			echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done

for var in OUT_ZIP MODULE_NAME MODULE_ID VERSION VERSION_CODE; do
	if [[ -z "${!var}" ]]; then
		echo "$var is required" >&2
		exit 1
	fi
done
if [[ "${#ORDERED_KOS[@]}" -eq 0 ]]; then
	echo "At least one --ko is required" >&2
	exit 1
fi
command -v modinfo >/dev/null 2>&1 || { echo "modinfo is required" >&2; exit 1; }

# Kernel release (vermagic minus the flags tail) — modules live under it so
# coexisting zips for different kernel releases do not collide.
KRELEASE="$(modinfo -F vermagic "${KO_PATHS[${ORDERED_KOS[0]}]}")"
KRELEASE="${KRELEASE%% *}"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE/system/lib/modules/$KRELEASE"
mkdir -p "$ROOT"

INSMOD_LINES=()
for key in "${ORDERED_KOS[@]}"; do
	ko="${KO_PATHS[$key]}"
	actual_name="$(modinfo -F name "$ko")"
	if [[ "$actual_name" != "$key" ]]; then
		echo "Module name mismatch: --ko key '$key' but modinfo says '$actual_name'" >&2
		exit 1
	fi
	vermagic="$(modinfo -F vermagic "$ko")"
	case "$vermagic" in
		"$KRELEASE"*) ;;
		*) echo "Module $key has mismatched vermagic: $vermagic" >&2; exit 1 ;;
	esac
	install -m 0644 "$ko" "$ROOT/$key.ko"
	deps="${DEP_LISTS[$key]:-}"
	if [[ -n "${SWAP_MODULES[$key]:-}" ]]; then
		if [[ -n "$deps" ]]; then
			INSMOD_LINES+=("swap_in \"/system/lib/modules/$KRELEASE/$key.ko\" \"$key\" \"$deps\"")
		else
			INSMOD_LINES+=("swap_in \"/system/lib/modules/$KRELEASE/$key.ko\" \"$key\"")
		fi
	elif [[ -n "$deps" ]]; then
		INSMOD_LINES+=("insmod_wait \"/system/lib/modules/$KRELEASE/$key.ko\" \"$deps\"")
	else
		INSMOD_LINES+=("insmod \"/system/lib/modules/$KRELEASE/$key.ko\"")
	fi
done

mkdir -p "$STAGE/META-INF/com/google/android"

if [[ -n "$FW_DIR" ]]; then
	mkdir -p "$STAGE/firmware"
	cp -r "$FW_DIR"/. "$STAGE/firmware/"
fi

cat >"$STAGE/module.prop" <<EOF
id=$MODULE_ID
name=$MODULE_NAME
version=$VERSION
versionCode=$VERSION_CODE
author=Loukious
description=${DESCRIPTION:-Extra kernel modules built for kernel release $KRELEASE.}
EOF

cat >"$STAGE/customize.sh" <<'EOF'
SKIPUNZIP=0
ui_print "- Extra kernel modules: $MODID"
EOF

cat >"$STAGE/service.sh" <<EOF
#!/system/bin/sh
# Insmod extra kernel modules (built for kernel release $KRELEASE).
# Runs at late_start service stage: vendor_dlkm's modules (cfg80211,
# mac80211, ...) are loaded from modules.load well before this point.
MODDIR=\${0%/*}
MODROOT="\$MODDIR/system/lib/modules/$KRELEASE"

insmod_wait() {
	# \$1: module path, \$2: comma-separated dependency module names
	local ko="\$1" deps="\$2" i
	for dep in \${deps//,/ }; do
		i=0
		while [ ! -d "/sys/module/\$dep" ] && [ \$i -lt 60 ]; do
			sleep 1
			i=\$((i + 1))
		done
		[ -d "/sys/module/\$dep" ] || \\
			log -p w -t "$MODULE_ID" "dependency \$dep not present, trying anyway"
	done
	insmod "\$ko" && log -p i -t "$MODULE_ID" "loaded \$(basename "\$ko")" ||
		log -p e -t "$MODULE_ID" "failed to load \$ko"
}

swap_in() {
	# \$1: our module path, \$2: stock module name to replace,
	# \$3 (optional): comma-separated dependency module names
	local ko="\$1" name="\$2" deps="\${3:-}" i wifi_on
	for dep in \${deps//,/ }; do
		i=0
		while [ ! -d "/sys/module/\$dep" ] && [ \$i -lt 60 ]; do
			sleep 1
			i=\$((i + 1))
		done
	done
	if [ ! -d "/sys/module/\$name" ]; then
		# Stock driver not loaded (Wi-Fi stack never came up): load ours
		# directly. Do NOT touch the Wi-Fi setting in this case.
		insmod "\$ko" && log -p i -t "$MODULE_ID" "loaded \$(basename "\$ko") (stock was not loaded)" ||
			log -p e -t "$MODULE_ID" "failed to load \$ko"
		return
	fi
	# Remember whether Wi-Fi was on so it is restored to the same state.
	wifi_on="\$(settings get global wifi_on 2>/dev/null)"
	[ "\$wifi_on" = "1" ] && svc wifi disable
	i=0
	while [ "\$(cat /sys/module/\$name/refcnt 2>/dev/null)" != "0" ] && [ \$i -lt 30 ]; do
		sleep 1
		i=\$((i + 1))
	done
	if [ "\$(cat /sys/module/\$name/refcnt 2>/dev/null)" = "0" ]; then
		rmmod "\$name"
		if insmod "\$ko"; then
			log -p i -t "$MODULE_ID" "swapped \$name for \$(basename "\$ko")"
		else
			log -p e -t "$MODULE_ID" "failed to load \$ko after rmmod \$name; restoring stock"
			insmod "/vendor_dlkm/lib/modules/\$name.ko" ||
				log -p e -t "$MODULE_ID" "could not restore stock \$name"
		fi
	else
		log -p e -t "$MODULE_ID" "refcount of \$name never reached 0; keeping stock driver"
	fi
	[ "\$wifi_on" = "1" ] && svc wifi enable
}

EOF
if [[ -n "$FW_DIR" ]]; then
	cat >>"$STAGE/service.sh" <<EOF
fw_setup() {
	# Make the shipped firmware (firmware/rtlwifi/*.bin) reachable by the
	# kernel's request_firmware(). The ROM's firmware search path is
	# /odm/firmware/o10u on a read-only erofs partition, so bind-mount an
	# overlay of it: the ROM's own files plus ours, labeled vendor_file
	# (the label the ROM's firmware carries) to satisfy enforcing SELinux.
	# Validated on onyx 2026-09-14 with SELinux ENFORCING: dongle probes,
	# firmware loads ("Firmware revision 80.0"), wlan1 registers. Both the
	# label AND the directory execute bits are load-bearing (see below).
	local ov="\$MODDIR/odm-firmware-overlay" rom="/odm/firmware/o10u" f
	mkdir -p "\$ov"
	for f in "\$rom"/*; do
		[ -f "\$f" ] && cp -f "\$f" "\$ov/" 2>/dev/null
	done
	rm -rf "\$ov/rtlwifi"
	cp -r "\$MODDIR/firmware/rtlwifi" "\$ov/"
	chown -R root:root "\$ov"
	# Modes per file TYPE, never by glob: "\$ov"/* matches the rtlwifi
	# DIRECTORY too, so a blanket "chmod 0644 \$ov/*" strips its execute bit
	# and makes it untraversable. The kernel reads firmware from a kworker in
	# the "kernel" SELinux domain, which policy denies CAP_DAC_OVERRIDE /
	# CAP_DAC_READ_SEARCH - so root cannot paper over the missing +x and
	# request_firmware() fails with -EACCES (-13) while the file itself is
	# perfectly labeled. Diagnosed on onyx 2026-09-14: the dongle probed,
	# read its MAC, then died at "loading .../rtl8192cufw_TMSC.bin failed
	# with error -13" under enforcing, and the only audit trace was a
	# dac_override denial against scontext=u:r:kernel:s0 (no AVC on the file).
	find "\$ov" -type d -exec chmod 0755 {} +
	find "\$ov" -type f -exec chmod 0644 {} +
	chcon -R u:object_r:vendor_file:s0 "\$ov" 2>/dev/null
	if grep -q " \$rom " /proc/mounts; then
		# Already mounted from a previous service run (e.g. KSU re-exec):
		# the overlay files were just refreshed in place, nothing to do.
		return
	fi
	if mount --bind "\$ov" "\$rom"; then
		log -p i -t "$MODULE_ID" "firmware overlay mounted at \$rom"
	else
		log -p e -t "$MODULE_ID" "firmware overlay mount at \$rom failed"
	fi
}

fw_setup
EOF
fi
for line in "${INSMOD_LINES[@]}"; do
	printf '%s\n' "$line" >>"$STAGE/service.sh"
done

# insmod'd modules block their own removal path; uninstall just drops the
# files. Modules installed with --swap revert to the stock driver on the
# next reboot (vendor_dlkm loads it again) — no restore needed here.
# The firmware overlay bind-mount (if any) is detached so the ROM's
# original /odm/firmware/o10u becomes visible again.
cat >"$STAGE/uninstall.sh" <<'EOF'
#!/system/bin/sh
# Modules stay resident until reboot; the files are removed with the module.
# Swapped drivers (qca_cld3 etc.) automatically revert to the stock
# vendor_dlkm driver on the next reboot.
if grep -q " /odm/firmware/o10u " /proc/mounts; then
	umount /odm/firmware/o10u 2>/dev/null
fi
true
EOF

cat >"$STAGE/META-INF/com/google/android/update-binary" <<'EOF'
#!/sbin/sh
# Minimal AnyKernel-free Magisk/KSU-compatible installer: the manager's own
# shim (Magisk >= 20.4 / KernelSU ksud installer.sh) handles everything when
# customize.sh exists; this file only exists for managers that require it.
SKIPUNZIP=0
. "$MODPATH/customize.sh"
EOF
cat >"$STAGE/META-INF/com/google/android/updater-script" <<'EOF'
# Not used by manager-based installs.
EOF

chmod 0755 "$STAGE/service.sh" "$STAGE/uninstall.sh" \
	"$STAGE/META-INF/com/google/android/update-binary"

mkdir -p "$(dirname "$OUT_ZIP")"
out_abs="$(cd "$(dirname "$OUT_ZIP")" && pwd)/$(basename "$OUT_ZIP")"
(cd "$STAGE" && zip -qr9 "$out_abs" .)

echo "[+] Created: $OUT_ZIP"
echo "    kernel release: $KRELEASE"
echo "    modules:        ${ORDERED_KOS[*]}"
ls -lh "$OUT_ZIP"
