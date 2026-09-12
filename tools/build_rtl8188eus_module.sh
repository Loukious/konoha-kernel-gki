#!/usr/bin/env bash
set -euo pipefail

# Build the SimplyCEO/rtl8188eus out-of-tree driver (Realtek RTL8188EUS USB
# adapter, monitor mode + frame injection) as a module against the prepared
# konoha-ABI kernel output.
#
# Everything this script does was validated locally against the same kind of
# modules_prepare'd tree (2026-09-11):
#   - the driver's CONFIG_PLATFORM_I386_PC wrapper supplies the needed
#     -DCONFIG_IOCTL_CFG80211 define set (it is a build-system selector, not
#     an architecture statement; ARCH/KSRC are overridden on the command line)
#   - clang needs -Wno-error: rtw_recv.h has two -Wuninitialized warnings
#     upstream never silenced
#   - GKI kernels export filp_open/kernel_read inside the
#     VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver namespace;
#     MODULE_IMPORT_NS must be added to os_intfs.c (idempotent)
#   - cfg80211 comes from a vendor module, so its CRCs resolve through the
#     same extra-symvers file the qcacld build uses (KBUILD_EXTRA_SYMBOLS)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_SRC="${KERNEL_SRC:-$ROOT_DIR/sources/xiaomi-kernel}"
KERNEL_OUT="${KERNEL_OUT:-$ROOT_DIR/out}"
DRIVER_SRC="${DRIVER_SRC:-$ROOT_DIR/sources/rtl8188eus}"
EXTRA_SYMVERS="${EXTRA_SYMVERS:-$ROOT_DIR/artifacts/wlan/vendor-abi/pixelos-qca-extra.symvers}"
OUT_KO="${1:-$ROOT_DIR/artifacts/wlan/8188eu.ko}"
JOBS="${JOBS:-$(nproc)}"

for var in KERNEL_SRC KERNEL_OUT DRIVER_SRC EXTRA_SYMVERS; do
	if [[ ! -e "${!var}" ]]; then
		echo "Missing $var: ${!var}" >&2
		exit 1
	fi
done

if [[ ! -f "$KERNEL_OUT/.config" || ! -f "$KERNEL_OUT/Module.symvers" ]]; then
	echo "Kernel output is incomplete: $KERNEL_OUT" >&2
	echo "Run the ABI prep (modules_prepare) before building this driver." >&2
	exit 1
fi

# The namespace import. MODULE_IMPORT_NS takes the namespace unquoted
# (it stringifies internally) - see include/linux/module.h.
if ! grep -q 'MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver)' \
	"$DRIVER_SRC/os_dep/linux/os_intfs.c"; then
	patch -s -p1 -d "$DRIVER_SRC" <<'PATCH'
--- a/os_dep/linux/os_intfs.c
+++ b/os_dep/linux/os_intfs.c
@@ -28,0 +29,4 @@ MODULE_VERSION(DRIVERVERSION);
+/* GKI kernels export kernel_read/file-IO symbols inside the
+ * VFS_internal namespace; modules using them must import it. */
+MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
+
PATCH
	echo "[+] Added VFS_internal namespace import"
else
	echo "[+] VFS_internal namespace import already present"
fi

# Optional debug logging (RTW_DEBUG=1). The driver is SILENT by default:
# with CONFIG_RTW_DEBUG unset, include/rtw_debug.h compiles every logging
# macro - RTW_PRINT/ERR/WARN/INFO/DBG, error paths included - down to
# "do {} while (0)", so a released module cannot report anything at all.
# Turning it on needs BOTH switches: the Makefile only adds the defines
# under `ifeq ($(CONFIG_RTW_DEBUG), y)`, and the level it bakes in comes
# from CONFIG_RTW_LOG_LEVEL, whose in-tree default of 0 (_DRV_NONE_) is
# still silent. Level 4 is _DRV_INFO_ (5 = _DRV_DEBUG_); once built this
# way, rtw_drv_log_level is also a module_param(0644) and so retunable at
# insmod time and at runtime without another build.
debug_args=()
case "${RTW_DEBUG:-0}" in
	1 | y | yes | on)
		debug_args=(
			CONFIG_RTW_DEBUG=y
			"CONFIG_RTW_LOG_LEVEL=${RTW_LOG_LEVEL:-4}"
		)
		echo "[+] Debug logging ENABLED (level ${RTW_LOG_LEVEL:-4}) - debug build, not for release"
		;;
	*)
		echo "[+] Debug logging disabled (release build; pass RTW_DEBUG=1 to enable)"
		;;
esac

# Optional bisect variants for the plug-freeze investigation (2026-09-12).
# The dead-boot capture shows the driver bound but idle - probe 100% clean,
# no hal_init, no URBs - and the kernel dying ~2.4s after probe with no oops
# text. The affected phone's bisect proved the freeze needs the driver
# BOUND (no extra modules = no freeze with the dongle attached), so the kill
# is a side effect of the bound state, not an executing code path. Two
# candidate mechanisms, one variant each:
#   a - noautopm: set supports_autosuspend unconditionally and drop the
#       probe's autopm get, so the dongle autosuspends ~2s after probe
#       exactly like the no-driver case. (usbcore enables interface runtime
#       PM only for drivers that opt in, and otherwise vetoes whole-device
#       autosuspend - so the release build pins the dongle U0/active
#       forever, the one real state delta vs the surviving boot.)
#       Tests the pinned-active theory. rtw_suspend/resume become no-ops
#       there (bup == 0 -> rtw_suspend_common exits early).
#   b - nonetdev: full probe but skip wiphy/netdev registration, so the
#       adapter stays invisible to the network stack (pnetdev stays
#       allocated; rtw_os_ndev_unregister is a no-op since
#       adapter->registered stays 0). Tests the theory that Wi-Fi HAL/netd
#       interface enumeration over netlink trips over the second wiphy -
#       the death timestamp coincides with WifiHalAidlImpl init completion.
# Both variants also add a PM-TRACE heartbeat (runtime-PM state of the
# interface and device every 500ms for 60s after probe) plus suspend/resume
# entry logs, because the death lands in the post-probe window where
# nothing else logs. Variants force debug logging on: the markers are
# RTW_* macros and would compile away otherwise.
variant="${RTW_VARIANT:-}"
case "$variant" in
	"" | plain | off)
		variant=""
		;;
	a | a-noautopm | A | noautopm)
		variant="a"
		;;
	b | b-nonetdev | B | nonetdev)
		variant="b"
		;;
	*)
		echo "Unknown RTW_VARIANT: $RTW_VARIANT (want a-noautopm or b-nonetdev)" >&2
		exit 1
		;;
esac

if [[ -n "$variant" && ${#debug_args[@]} -eq 0 ]]; then
	debug_args=(CONFIG_RTW_DEBUG=y "CONFIG_RTW_LOG_LEVEL=${RTW_LOG_LEVEL:-4}")
	echo "[+] RTW_VARIANT=$variant: forcing debug logging on (markers must not compile away)"
fi

variant_cflags=()
if [[ -n "$variant" ]]; then
	case "$variant" in
		a) marker="VARIANT-A" ;;
		b) marker="VARIANT-B" ;;
	esac
	if grep -q "$marker" "$DRIVER_SRC/os_dep/linux/usb_intf.c"; then
		echo "[+] $marker patch already present"
	else
		case "$variant" in
			a)
				patch -s -p1 -d "$DRIVER_SRC" <<'PATCH'
--- a/os_dep/linux/usb_intf.c
+++ b/os_dep/linux/usb_intf.c
@@ -302,9 +302,12 @@
 #if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
 	.usbdrv.reset_resume   = rtw_resume,
 #endif
-#ifdef CONFIG_AUTOSUSPEND
+	/* VARIANT-A: unconditional. usb_probe_interface() enables interface
+	 * runtime PM only when the bound driver opts in; without this flag
+	 * usbcore also vetoes whole-device autosuspend, pinning the dongle
+	 * in U0/active forever (the one real state delta vs the surviving
+	 * no-driver boot). */
 	.usbdrv.supports_autosuspend = 1,
-#endif
 
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) && LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0))
 	.usbdrv.drvwrap.driver.shutdown = rtw_dev_shutdown,
@@ -932,6 +935,8 @@
 	pdbgpriv = &dvobj->drv_dbg;
 	padapter = dvobj_get_primary_adapter(dvobj);
 
+	RTW_INFO("rtw_suspend enter event=0x%x\n", message.event);
+
 	if (pwrpriv->bInSuspend == _TRUE) {
 		RTW_INFO("%s bInSuspend = %d\n", __func__, pwrpriv->bInSuspend);
 		pdbgpriv->dbg_suspend_error_cnt++;
@@ -1043,6 +1048,8 @@
 	dvobj = usb_get_intfdata(pusb_intf);
 	pwrpriv = dvobj_to_pwrctl(dvobj);
 	pdbgpriv = &dvobj->drv_dbg;
+
+	RTW_INFO("rtw_resume enter\n");
 	padapter = dvobj_get_primary_adapter(dvobj);
 	pmlmeext = &padapter->mlmeextpriv;
 
@@ -1315,8 +1322,10 @@
 #endif
 	/* 2012-07-11 Move here to prevent the 8723AS-VAU BT auto suspend influence */
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
-	if (usb_autopm_get_interface(pusb_intf) < 0)
-		RTW_INFO("can't get autopm:\n");
+	/* VARIANT-A: autopm get dropped - usage count stays 0 so the dongle
+	 * autosuspends ~2s after probe, exactly like the no-driver case.
+	 * rtw_suspend/rtw_resume then run as no-ops (bup == 0). */
+	RTW_INFO("VARIANT-A: autopm get skipped, dongle left to runtime PM\n");
 #endif
 #ifdef CONFIG_BT_COEXIST
 	dvobj_to_pwrctl(dvobj)->autopm_cnt = 1;
@@ -1412,6 +1421,52 @@
 
 }
 
+#if defined(RTW_PM_TRACE)
+/* Diagnostic heartbeat: log the runtime-PM state of the interface and the
+ * device every 500ms for the first minute after probe, so a silent kernel
+ * death inside the post-probe window still leaves the last observed PM
+ * state in the log (freeze bisect, 2026-09-12). */
+static void rtw_pm_trace_fn(struct work_struct *work);
+static DECLARE_DELAYED_WORK(rtw_pm_trace_work, rtw_pm_trace_fn);
+static struct usb_interface *rtw_pm_trace_intf;
+static int rtw_pm_trace_tick;
+static void rtw_pm_trace_fn(struct work_struct *work)
+{
+	struct usb_interface *intf = rtw_pm_trace_intf;
+	struct usb_device *udev;
+
+	if (!intf)
+		return;
+	if (++rtw_pm_trace_tick > 120)
+		return;
+	udev = interface_to_usbdev(intf);
+	RTW_INFO("PM-TRACE tick=%d jiffies=%lu intf[usage=%d disable=%d status=%d] udev[status=%d state=0x%x remote_wakeup=%d]\n",
+			 rtw_pm_trace_tick, jiffies,
+			 atomic_read(&intf->dev.power.usage_count),
+			 intf->dev.power.disable_depth,
+			 (int)intf->dev.power.runtime_status,
+			 (int)udev->dev.power.runtime_status,
+			 (int)udev->state,
+			 udev->do_remote_wakeup);
+	schedule_delayed_work(&rtw_pm_trace_work, msecs_to_jiffies(500));
+}
+static void rtw_pm_trace_start(struct usb_interface *intf)
+{
+	rtw_pm_trace_intf = intf;
+	rtw_pm_trace_tick = 0;
+	schedule_delayed_work(&rtw_pm_trace_work, msecs_to_jiffies(500));
+	RTW_INFO("PM-TRACE started\n");
+}
+static void rtw_pm_trace_stop(void)
+{
+	cancel_delayed_work_sync(&rtw_pm_trace_work);
+	rtw_pm_trace_intf = NULL;
+}
+#else
+static inline void rtw_pm_trace_start(struct usb_interface *intf) {}
+static inline void rtw_pm_trace_stop(void) {}
+#endif
+
 static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
 {
 	_adapter *padapter = NULL;
@@ -1468,6 +1523,8 @@
 	if (rtw_os_ndevs_init(dvobj) != _SUCCESS)
 		goto free_if_vir;
 
+	rtw_pm_trace_start(pusb_intf);
+
 #ifdef CONFIG_HOSTAPD_MLME
 	hostapd_mode_init(padapter);
 #endif
@@ -1523,6 +1580,8 @@
 
 	dvobj->processing_dev_remove = _TRUE;
 
+	rtw_pm_trace_stop();
+
 	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
 	rtw_os_ndevs_unregister(dvobj);
 
@@ -1589,6 +1648,7 @@
 	int ret = 0;
 
 	RTW_PRINT("module init start\n");
+	RTW_PRINT("VARIANT-A: noautopm (autosuspend enabled, probe autopm get dropped)\n");
 	dump_drv_version(RTW_DBGDUMP);
 #ifdef BTCOEXVERSION
 	RTW_PRINT(DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
PATCH
				;;
			b)
				patch -s -p1 -d "$DRIVER_SRC" <<'PATCH'
--- a/os_dep/linux/usb_intf.c
+++ b/os_dep/linux/usb_intf.c
@@ -932,6 +932,8 @@
 	pdbgpriv = &dvobj->drv_dbg;
 	padapter = dvobj_get_primary_adapter(dvobj);
 
+	RTW_INFO("rtw_suspend enter event=0x%x\n", message.event);
+
 	if (pwrpriv->bInSuspend == _TRUE) {
 		RTW_INFO("%s bInSuspend = %d\n", __func__, pwrpriv->bInSuspend);
 		pdbgpriv->dbg_suspend_error_cnt++;
@@ -1043,6 +1045,8 @@
 	dvobj = usb_get_intfdata(pusb_intf);
 	pwrpriv = dvobj_to_pwrctl(dvobj);
 	pdbgpriv = &dvobj->drv_dbg;
+
+	RTW_INFO("rtw_resume enter\n");
 	padapter = dvobj_get_primary_adapter(dvobj);
 	pmlmeext = &padapter->mlmeextpriv;
 
@@ -1412,6 +1416,52 @@
 
 }
 
+#if defined(RTW_PM_TRACE)
+/* Diagnostic heartbeat: log the runtime-PM state of the interface and the
+ * device every 500ms for the first minute after probe, so a silent kernel
+ * death inside the post-probe window still leaves the last observed PM
+ * state in the log (freeze bisect, 2026-09-12). */
+static void rtw_pm_trace_fn(struct work_struct *work);
+static DECLARE_DELAYED_WORK(rtw_pm_trace_work, rtw_pm_trace_fn);
+static struct usb_interface *rtw_pm_trace_intf;
+static int rtw_pm_trace_tick;
+static void rtw_pm_trace_fn(struct work_struct *work)
+{
+	struct usb_interface *intf = rtw_pm_trace_intf;
+	struct usb_device *udev;
+
+	if (!intf)
+		return;
+	if (++rtw_pm_trace_tick > 120)
+		return;
+	udev = interface_to_usbdev(intf);
+	RTW_INFO("PM-TRACE tick=%d jiffies=%lu intf[usage=%d disable=%d status=%d] udev[status=%d state=0x%x remote_wakeup=%d]\n",
+			 rtw_pm_trace_tick, jiffies,
+			 atomic_read(&intf->dev.power.usage_count),
+			 intf->dev.power.disable_depth,
+			 (int)intf->dev.power.runtime_status,
+			 (int)udev->dev.power.runtime_status,
+			 (int)udev->state,
+			 udev->do_remote_wakeup);
+	schedule_delayed_work(&rtw_pm_trace_work, msecs_to_jiffies(500));
+}
+static void rtw_pm_trace_start(struct usb_interface *intf)
+{
+	rtw_pm_trace_intf = intf;
+	rtw_pm_trace_tick = 0;
+	schedule_delayed_work(&rtw_pm_trace_work, msecs_to_jiffies(500));
+	RTW_INFO("PM-TRACE started\n");
+}
+static void rtw_pm_trace_stop(void)
+{
+	cancel_delayed_work_sync(&rtw_pm_trace_work);
+	rtw_pm_trace_intf = NULL;
+}
+#else
+static inline void rtw_pm_trace_start(struct usb_interface *intf) {}
+static inline void rtw_pm_trace_stop(void) {}
+#endif
+
 static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
 {
 	_adapter *padapter = NULL;
@@ -1468,6 +1518,8 @@
 	if (rtw_os_ndevs_init(dvobj) != _SUCCESS)
 		goto free_if_vir;
 
+	rtw_pm_trace_start(pusb_intf);
+
 #ifdef CONFIG_HOSTAPD_MLME
 	hostapd_mode_init(padapter);
 #endif
@@ -1523,6 +1575,8 @@
 
 	dvobj->processing_dev_remove = _TRUE;
 
+	rtw_pm_trace_stop();
+
 	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
 	rtw_os_ndevs_unregister(dvobj);
 
@@ -1589,6 +1643,7 @@
 	int ret = 0;
 
 	RTW_PRINT("module init start\n");
+	RTW_PRINT("VARIANT-B: nonetdev (probe complete, wiphy/netdev registration skipped)\n");
 	dump_drv_version(RTW_DBGDUMP);
 #ifdef BTCOEXVERSION
 	RTW_PRINT(DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
--- a/os_dep/linux/os_intfs.c
+++ b/os_dep/linux/os_intfs.c
@@ -3113,14 +3113,15 @@
 	if (rtw_os_ndevs_alloc(dvobj) != _SUCCESS)
 		goto exit;
 
-	if (rtw_os_ndevs_register(dvobj) != _SUCCESS)
-		goto os_ndevs_free;
+	/* VARIANT-B: registration skipped for the freeze bisect - wiphy and
+	 * netdev stay allocated (pnetdev valid for suspend/remove) but are
+	 * never registered, so the adapter is invisible to the network
+	 * stack. rtw_os_ndev_unregister is a no-op here because
+	 * adapter->registered stays 0. */
+	RTW_INFO("VARIANT-B: skipping ndev/wiphy registration\n");
 
 	ret = _SUCCESS;
 
-os_ndevs_free:
-	if (ret != _SUCCESS)
-		rtw_os_ndevs_free(dvobj);
 exit:
 	return ret;
 }
PATCH
				;;
		esac
		echo "[+] Applied $marker diagnostic variant patch"
	fi
	variant_cflags=(-DRTW_PM_TRACE)
fi

echo "[+] Building 8188eu (jobs: $JOBS)"
# The Realtek Makefile wraps an ordinary external-module build:
#   make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -C $KSRC M=$(pwd) modules
# Every KSRC/ARCH/toolchain variable passed here overrides the Makefile's
# own (command-line assignments win), which is how we reuse the I386_PC
# platform block (it only contributes the CONFIG_IOCTL_CFG80211 defines).
make -C "$DRIVER_SRC" -j"$JOBS" \
	${debug_args[@]+"${debug_args[@]}"} \
	ARCH=arm64 \
	KSRC="$KERNEL_OUT" \
	CC=clang \
	CROSS_COMPILE=llvm- \
	HOSTCC=clang \
	LD=ld.lld \
	AR=llvm-ar \
	NM=llvm-nm \
	STRIP=llvm-strip \
	OBJCOPY=llvm-objcopy \
	OBJDUMP=llvm-objdump \
	READELF=llvm-readelf \
	HOSTAR=llvm-ar \
	HOSTLD=ld.lld \
	KBUILD_EXTRA_SYMBOLS="$EXTRA_SYMVERS" \
	CONFIG_PLATFORM_I386_PC=y \
	USER_EXTRA_CFLAGS="-Wno-error -DCONFIG_LITTLE_ENDIAN -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT ${variant_cflags[*]}" \
	modules

BUILT_KO="$DRIVER_SRC/8188eu.ko"
if [[ ! -f "$BUILT_KO" ]]; then
	echo "Driver did not produce $BUILT_KO" >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT_KO")"
if command -v llvm-strip >/dev/null 2>&1; then
	STRIP_BIN=llvm-strip
else
	STRIP_BIN=strip
fi
"$STRIP_BIN" --strip-unneeded "$BUILT_KO" -o "$OUT_KO"

kernel_release="$(cat "$KERNEL_OUT/include/config/kernel.release")"
module_vermagic="$(modinfo -F vermagic "$OUT_KO")"
case "$module_vermagic" in
	"$kernel_release"*) ;;
	*)
		echo "8188eu vermagic does not match the prepared kernel" >&2
		echo "  kernel release:  $kernel_release" >&2
		echo "  module vermagic: $module_vermagic" >&2
		exit 1
		;;
esac

if [[ "$(modinfo -F depends "$OUT_KO")" != "cfg80211" ]]; then
	echo "Unexpected 8188eu dependencies: $(modinfo -F depends "$OUT_KO")" >&2
	exit 1
fi

# Prove the logging switch landed rather than trusting it. When the macros
# are compiled out their format strings vanish from .rodata entirely, so the
# presence of one is a direct read of what the binary can actually print.
# (llvm-strip drops symbols, not string literals, so this survives the strip.)
log_marker="nr_endpoint="
if [[ ${#debug_args[@]} -gt 0 ]]; then
	if ! grep -aqF "$log_marker" "$OUT_KO"; then
		echo "RTW_DEBUG was requested but the module carries no log strings" >&2
		exit 1
	fi
	log_state="enabled (level ${RTW_LOG_LEVEL:-4})"
else
	if grep -aqF "$log_marker" "$OUT_KO"; then
		echo "Release build unexpectedly carries debug log strings" >&2
		exit 1
	fi
	log_state="compiled out"
fi

# Same proof-by-rodata for the diagnostic variants: the marker strings must
# be present in the .ko when a variant was requested, and absent otherwise
# (a variant that silently failed to patch would poison the bisect).
case "$variant" in
	a | b)
		for marker_string in "$marker:" "PM-TRACE tick="; do
			if ! grep -aqF "$marker_string" "$OUT_KO"; then
				echo "RTW_VARIANT=$variant was requested but the module lacks \"$marker_string\"" >&2
				exit 1
			fi
		done
		echo "    variant:   $marker"
		;;
	"")
		if grep -aqF "PM-TRACE tick=" "$OUT_KO" || grep -aqE "VARIANT-[AB]:" "$OUT_KO"; then
			echo "Non-variant build unexpectedly carries variant diagnostic strings" >&2
			exit 1
		fi
		;;
esac

echo "[+] Created: $OUT_KO"
echo "    vermagic:  $module_vermagic"
echo "    logging:   $log_state"
echo "    size:      $(stat -c %s "$OUT_KO") bytes"
