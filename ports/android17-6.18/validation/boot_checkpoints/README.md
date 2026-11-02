# GPU cooling dependency and boot checkpoints

Patch 0138 checks the GPU supplier after adding a managed device link. A nonblocking supplier lock protects device_is_bound; an unbound or busy supplier returns -EPROBE_DEFER and releases the lookup references. The core removes the failed consumer link. Frequency/QoS work only starts after successful supplier validation. This corrects the captured device-link warning and is not established as the reset fix.

The exact production helper passes five sanitizer-backed host lifecycle cases and rejects four compiled mutations. The fixture models driver-core managed-link success and device-lock ownership. It does not execute real probe/unbind scheduling or GPU firmware.

Patch 0139 adds disabled-by-default read-only boot_trace module parameters to i2c_msm_geni, ipam and qcom_wdt_core. It records I2C resource/adapter and client registration, IPA device/initialization phases and watchdog timer/IPI/reset progress. Entry/exit wrappers preserve original probe arguments and return values. The existing hardware timeouts and calls remain intact.

The new image option --boot-checkpoints requires --diagnostic-boot, validates the module parameters and existing GKI lockup/hung-task capabilities, then explicitly records the tracing parameters and diagnostic lockup settings in the boot command line and manifest. It uses normal module loading with no blacklist. Android libmodprobe ModuleConfig::ParseKernelCmdlineOptions passes the module-specific boot_trace options to module loading. Soft/hard lockup panics and a five-second hung-task diagnostic attempt to capture backtraces before the 11-second hardware watchdog bark. A stall that prevents those detectors from running may still lack a software trace.

Offline build, import CRC, packaging and live hardware results are separate gates and are recorded as they complete.
