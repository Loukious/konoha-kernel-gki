# Native WCD9xxx and headset support

The two modules retain the six native compilation units, twenty exports,
provider dependencies, aliases, parameters, clock tables and calibration UAPI.
The build uses fifteen exact source/header pins and eight existing provider
headers. Both modules compile cleanly against the pinned 6.18 GKI, with 95
matching imported-symbol CRCs.

The reference-clock provider uses managed clock and device-tree registration.
Cleanup withdraws the provider and clock before releasing votes, mappings and
pinctrl storage. It preserves probe deferral, retains failed releases for retry,
and rolls back a newly acquired clock when selecting active pins fails. Parent
selection uses retained immutable initialization data because the clock core
clears `clk_hw.init` after registration. Native clock IDs, rates, parent choices
and successful request ordering are unchanged. Class-H and calibration helper
implementations remain native.

MBHC initializes callback storage before publication, gates IRQ/work production
until start and after stop, drains notifiers and pending work, and releases its
own DT references. Failed initialization can be retried. Stop restores the
original calibration pointer before releasing firmware. The codec owner must
serialize lifecycle calls and call deinit before freeing its component or IRQ
domain. Details are in the accompanying MBHC port record.

The clock suite passes 21 lifecycle and failure scenarios and rejects five
compiled regressions. The MBHC suite passes 26 scenarios and rejects eight
compiled regressions. They execute selected production source against mocked
kernel services with AddressSanitizer and UndefinedBehaviorSanitizer. Exact
source replay, generated compilation units, configuration and native metadata
have also been checked. Strict checkpatch reports zero errors and one advisory
for the preserved native debugfs mode `0666`.

Physical audio, DSP/firmware, real kernel concurrency and phone boot remain
untested. CCF and codec owners must quiesce their users before teardown;
persistent hardware release failures are logged and can require recovery.
The native `CONFIG_AUDIO_QGKI` option remains disabled, so calibration upload
through the hwdep interface is not added or claimed.
