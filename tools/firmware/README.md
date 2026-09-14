rtlwifi firmware blobs shipped for the rtl8xxxu module (see
tools/build_rtl8xxxu_module.sh). These are the exact files the driver
declares via MODULE_FIRMWARE, taken from the upstream linux-firmware
repository and redistributable under its LICENSE (Linux Firmware
License / Redistributable in binary form).

  rtl8192cufw_{TMSC,A,B}.bin — RTL8188CU/8192CU (0bda:8176 etc.):
      device-validated on onyx 2026-09-14 (probe, firmware load,
      scan under enforcing SELinux).
  the rest — other chips the module claims under
      CONFIG_RTL8XXXU_UNTESTED; shipped so the same module zip
      supports them, but not device-tested.

Not shipped (module requests it only when enable_bluetooth=1 demo
mode is enabled, which is off by default):
  rtl8723bu_bt.bin — not in mainline linux-firmware.
