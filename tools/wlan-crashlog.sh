#!/system/bin/sh

LOG_ROOT=/data/adb/wlan-crashlogs
BOOT_ID="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
mkdir -p "$LOG_ROOT"

RUN_MARK="${LOG_ROOT}/.capture-${BOOT_ID:-unknown-boot}"
if ! mkdir "$RUN_MARK" 2>/dev/null; then
  exit 0
fi

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
    dmesg -w >"$RUN_DIR/dmesg-live.txt" 2>&1 || {
      while :; do
        echo "=== dmesg fallback $(date 2>/dev/null) uptime $(cat /proc/uptime 2>/dev/null) ==="
        dmesg 2>/dev/null | tail -800
        sleep 1
      done
    }
  ) &
  DMESG_PID=$!

  echo "ADB/USB forcing disabled; logger only records state." \
    >"$RUN_DIR/adb-enable.log"

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
  while [ "$i" -lt 480 ]; do
    {
      echo "=== sample $i $(date 2>/dev/null) ==="
      echo "uptime=$(cat /proc/uptime 2>/dev/null)"
      getprop sys.boot_completed init.svc.vendor.wifi_hal init.svc.wpa_supplicant \
        init.svc.wificond init.svc.netd init.svc.surfaceflinger init.svc.bootanim \
        init.svc.zygote init.svc.system_server sys.usb.config sys.usb.state 2>/dev/null
      cat /proc/modules 2>/dev/null | grep -E 'qca|wlan|cfg80211|cnss|icnss' || true
      ip link show 2>/dev/null || true
      ps -A -T 2>/dev/null | grep -E 'system_server|surfaceflinger|wpa|wifi|wlan|vendor|ksu|init' || true
      cat /proc/interrupts 2>/dev/null | grep -Ei 'wlan|qca|icnss|cnss|ipa|glink|smp2p|qrtr' || true
    } >>"$RUN_DIR/samples.txt" 2>&1

    if [ "$i" = 6 ] || [ "$i" = 12 ]; then
      echo w >/proc/sysrq-trigger 2>/dev/null || true
    fi

    {
      echo "=== dmesg tail sample $i $(date 2>/dev/null) uptime $(cat /proc/uptime 2>/dev/null) ==="
      dmesg 2>/dev/null | tail -240
    } >>"$RUN_DIR/dmesg-tail-ring.txt" 2>&1
    dmesg 2>/dev/null | tail -800 >"$RUN_DIR/dmesg-tail-latest.txt"
    sync
    i=$((i + 1))
    sleep 0.5 2>/dev/null || sleep 1
  done

  kill "$DMESG_PID" 2>/dev/null || true
  dmesg >"$RUN_DIR/dmesg-final.txt" 2>&1
  getprop >"$RUN_DIR/getprop-final.txt" 2>/dev/null
  sync
) &

exit 0
