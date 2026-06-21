#!/usr/bin/env bash
set -euo pipefail

ADB_BIN="${ADB:-}"
if [[ -z "${ADB_BIN}" ]]; then
  if [[ -x /mnt/c/platform-tools/adb.exe ]]; then
    ADB_BIN=/mnt/c/platform-tools/adb.exe
  else
    ADB_BIN=adb
  fi
fi

SCRIPT_NAME="wlan-crashlog.sh"
REMOTE_SCRIPT="/data/adb/post-fs-data.d/${SCRIPT_NAME}"
REMOTE_LOG_ROOT="/data/adb/wlan-crashlogs"

usage() {
  cat <<USAGE
Usage: $0 install|remove|pull [output-dir]

Commands:
  install      Install the KernelSU early-boot Wi-Fi crash logger.
  remove       Remove the logger from /data/adb/post-fs-data.d.
  pull [dir]   Pull saved logs from ${REMOTE_LOG_ROOT}.

Set ADB=/path/to/adb if auto-detection is wrong.
USAGE
}

adb_shell_root() {
  "${ADB_BIN}" shell "su -c '$*'"
}

require_root_adb() {
  "${ADB_BIN}" wait-for-device
  adb_shell_root "id" | grep -q "uid=0" || {
    echo "KernelSU root via adb is not available." >&2
    exit 1
  }
}

install_logger() {
  require_root_adb

  local tmp_payload
  tmp_payload="$(mktemp)"
  trap 'rm -f "${tmp_payload}"' RETURN

  cat > "${tmp_payload}" <<'PAYLOAD'
#!/system/bin/sh

LOG_ROOT=/data/adb/wlan-crashlogs
BOOT_ID="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
STAMP="$(date +%Y%m%d-%H%M%S 2>/dev/null)"
[ -n "$STAMP" ] || STAMP=unknown-time
RUN_DIR="${LOG_ROOT}/${STAMP}-${BOOT_ID:-no-bootid}"

mkdir -p "$RUN_DIR"
chmod 700 "$LOG_ROOT" "$RUN_DIR" 2>/dev/null

(
  exec >>"$RUN_DIR/boot-capture.log" 2>&1

  echo "=== wlan crash logger start ==="
  date
  echo "boot_id=${BOOT_ID:-unknown}"
  echo "slot=$(getprop ro.boot.slot_suffix 2>/dev/null)"
  echo "kernel=$(uname -a 2>/dev/null)"

  mkdir -p "$RUN_DIR/pstore"
  if [ -d /sys/fs/pstore ]; then
    for f in /sys/fs/pstore/*; do
      [ -e "$f" ] && cp -p "$f" "$RUN_DIR/pstore/" 2>/dev/null
    done
  fi

  [ -e /proc/last_kmsg ] && cat /proc/last_kmsg >"$RUN_DIR/last_kmsg.txt" 2>/dev/null
  cat /proc/cmdline >"$RUN_DIR/cmdline.txt" 2>/dev/null
  getprop >"$RUN_DIR/getprop-start.txt" 2>/dev/null
  mount >"$RUN_DIR/mount.txt" 2>/dev/null

  if [ -d /vendor_dlkm/lib/modules ]; then
    ls -lZ /vendor_dlkm/lib/modules >"$RUN_DIR/vendor-dlkm-modules-ls.txt" 2>&1
  fi

  WIFI_KO=/vendor_dlkm/lib/modules/qca_cld3_wcn7750.ko
  if [ -e "$WIFI_KO" ]; then
    sha256sum "$WIFI_KO" >"$RUN_DIR/qca-ko-sha256.txt" 2>&1
    grep -a -o 'wlan/qca_cld/[^[:space:]]*WCNSS_qcom_cfg.ini' "$WIFI_KO" \
      >"$RUN_DIR/qca-ko-resource-paths.txt" 2>/dev/null
  fi

  dmesg >"$RUN_DIR/dmesg-start.txt" 2>&1
  (
    dmesg -w >"$RUN_DIR/dmesg-live.txt" 2>&1
  ) &
  DMESG_PID=$!

  (
    i=0
    while [ "$i" -lt 180 ]; do
      setprop persist.sys.usb.config mtp,adb 2>/dev/null
      setprop sys.usb.config mtp,adb 2>/dev/null
      settings put global adb_enabled 1 2>/dev/null
      svc usb setFunctions mtp,adb 2>/dev/null
      i=$((i + 1))
      sleep 1
    done
  ) >"$RUN_DIR/adb-enable.log" 2>&1 &

  (
    i=0
    while [ "$i" -lt 60 ]; do
      logcat -g >/dev/null 2>&1 && break
      i=$((i + 1))
      sleep 1
    done
    logcat -b all -v threadtime -d >"$RUN_DIR/logcat-start.txt" 2>&1
    logcat -b all -v threadtime >"$RUN_DIR/logcat-live.txt" 2>&1 &
    LOGCAT_PID=$!
    sleep 180
    kill "$LOGCAT_PID" 2>/dev/null
  ) &

  i=0
  while [ "$i" -lt 240 ]; do
    {
      echo "=== sample $i $(date 2>/dev/null) ==="
      getprop sys.boot_completed init.svc.vendor.wifi_hal init.svc.wpa_supplicant \
        init.svc.wificond init.svc.netd sys.usb.config sys.usb.state 2>/dev/null
      cat /proc/modules 2>/dev/null | grep -E 'qca|wlan|cfg80211|cnss|icnss' || true
      ip link show 2>/dev/null || true
    } >>"$RUN_DIR/samples.txt" 2>&1

    dmesg 2>/dev/null | tail -400 >"$RUN_DIR/dmesg-tail-latest.txt"
    sync
    i=$((i + 1))
    sleep 1
  done

  kill "$DMESG_PID" 2>/dev/null
  dmesg >"$RUN_DIR/dmesg-final.txt" 2>&1
  getprop >"$RUN_DIR/getprop-final.txt" 2>/dev/null
  sync
) &

exit 0
PAYLOAD

  "${ADB_BIN}" push "${tmp_payload}" /data/local/tmp/"${SCRIPT_NAME}" >/dev/null
  adb_shell_root "mkdir -p /data/adb/post-fs-data.d ${REMOTE_LOG_ROOT}; cp /data/local/tmp/${SCRIPT_NAME} ${REMOTE_SCRIPT}; chmod 0755 ${REMOTE_SCRIPT}; rm -f /data/local/tmp/${SCRIPT_NAME}; ls -l ${REMOTE_SCRIPT}"
  echo "Installed ${REMOTE_SCRIPT}"
  echo "Logs will be saved under ${REMOTE_LOG_ROOT} on each boot."
}

remove_logger() {
  require_root_adb
  adb_shell_root "rm -f ${REMOTE_SCRIPT}; ls -l ${REMOTE_SCRIPT} 2>/dev/null || true"
  echo "Removed ${REMOTE_SCRIPT}"
}

pull_logs() {
  require_root_adb
  local out_dir="${1:-artifacts/wlan-crashlogs}"
  mkdir -p "${out_dir}"
  adb_shell_root "ls -la ${REMOTE_LOG_ROOT} 2>/dev/null || true"
  "${ADB_BIN}" pull "${REMOTE_LOG_ROOT}" "${out_dir}/" >/dev/null
  echo "Pulled logs to ${out_dir}/wlan-crashlogs"
}

case "${1:-}" in
  install)
    install_logger
    ;;
  remove)
    remove_logger
    ;;
  pull)
    pull_logs "${2:-}"
    ;;
  -h|--help|help|"")
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
