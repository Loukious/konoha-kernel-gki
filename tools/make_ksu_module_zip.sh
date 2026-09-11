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

OUT_ZIP=""
MODULE_NAME=""
MODULE_ID=""
VERSION=""
VERSION_CODE=""
DESCRIPTION=""
declare -A KO_PATHS DEP_LISTS
ORDERED_KOS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--out) OUT_ZIP="$2"; shift 2 ;;
		--module-name) MODULE_NAME="$2"; shift 2 ;;
		--module-id) MODULE_ID="$2"; shift 2 ;;
		--version) VERSION="$2"; shift 2 ;;
		--versionCode) VERSION_CODE="$2"; shift 2 ;;
		--description) DESCRIPTION="$2"; shift 2 ;;
		--ko)
			key="${2%%=*}"; path="${2#*=}"
			[[ "$key" != "$2" && -n "$key" && -n "$path" ]] || {
				echo "--ko expects NAME=path/to/NAME.ko" >&2; exit 1; }
			[[ -f "$path" ]] || { echo "Module not found: $path" >&2; exit 1; }
			KO_PATHS["$key"]="$path"
			ORDERED_KOS+=("$key")
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
	if [[ -n "$deps" ]]; then
		INSMOD_LINES+=("insmod_wait \"/system/lib/modules/$KRELEASE/$key.ko\" \"$deps\"")
	else
		INSMOD_LINES+=("insmod \"/system/lib/modules/$KRELEASE/$key.ko\"")
	fi
done

mkdir -p "$STAGE/META-INF/com/google/android"

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

EOF
for line in "${INSMOD_LINES[@]}"; do
	printf '%s\n' "$line" >>"$STAGE/service.sh"
done

# insmod'd modules block their own removal path; uninstall just drops the
# files (a reboot finishes the job).
cat >"$STAGE/uninstall.sh" <<'EOF'
#!/system/bin/sh
# Modules stay resident until reboot; the files are removed with the module.
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
