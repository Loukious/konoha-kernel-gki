The native LPASS RX macro uses one exact C input from modules revision
`8500748dae32f10507c02f1c94bc971553cb525b`, blob
`82552f5c865097e853ca44147fb1aca93084df16`. It reuses nine already ported
audio headers and the existing CDC, WCD-core and SoundWire controller modules.
The 6.18 changes include the const DAI channel-map callback and void platform
remove callback. Native DAI, rate, format, mixer, DAPM and route tables, the
single exported API, module parameters and provider dependency names remain.

Child setup is synchronous and bounded to the native three-child limit. Every
registered child and OF reference has an owner; partial setup unwinds in reverse
order. SoundWire notification targets are withdrawn and active calls drain
before child removal. Notifications hold no mutex across controller callbacks,
so controller reentry is supported. A notification must not synchronously remove
the provider whose active call is being drained. The native first-controller
notification policy is retained.

Runtime PM failures are checked and failed core-vote checks drop their PM
reference. The first and last RX clock transitions preserve the native register
sequence; register errors restore saved state, counters commit only on success,
and failed clock rollback retains its exact request for retry. DAPM owns one
idempotent request. SoundWire tracks pin, MCLK and controller-clock ownership
separately so partial disable can be retried. SSR clock reset retains a failed
temporary release. PM is initialized before macro publication, and final PM
status changes only after a successful final suspend.

FIR controls serialize programming, capability changes and cleanup. Repeated
enable requests neither duplicate clock references nor restart the filter.
Programming checks signed errors, validates group/path bounds, writes complete
coefficient pairs with zero padding for an odd final coefficient, and restores
the prior running filter register state. A RAM reset causes all cached groups
to be rewritten. Failed temporary MCLK release remains owned for retry. The
native 404-byte coefficient-group format and one-byte total-count format are
unchanged. Unsupported FIR hardware keeps the native no-op policy.

Component setup publishes callback state before DAPM construction and unwinds
its owned requests on failure. Component teardown stops FIR and releases only
its own DAPM and temporary FIR votes; it does not consume SoundWire votes.
Physical removal drains children and unregisters the macro before final clock
and PM cleanup. Persistent register/provider failures are reported. If a FIR
stop fails, physical removal releases its final clock reference after software
clients have drained; successful hardware shutdown in that case is not claimed.

Four production-source ASan/UBSan suites pass and reject 32 compiled deliberate
regressions. Children cover eight setup failure sites, five child counts,
notification reentry and 100 withdrawal races. Resource tests cover 63 cases,
49 transition failure sites and 800 concurrent MCLK cycles. FIR covers 183
cases, 62 coefficient-programming failure sites and all 202 path/count
combinations. Cleanup covers 40 cases including 29 initialization failure sites
and four SSR-reset failure sites. Exact source replay, clean compilation,
unchanged public exports/dependencies and imported-symbol CRC checks are also
required before freezing.

Run each installed verifier with `--source-root WORKSPACE/onyx --negative
--output REPORT.json`. `verify_rx.py` takes those options directly. The other
three (`verify_rx_resources.py`, `verify_rx_fir.py`, `verify_rx_cleanup.py`) also
take `WORKSPACE` as their first positional argument. Their fixtures are
`tests/audio_rx_*`; the FIR and cleanup fixtures include the shared resource
fixture. The validation records hash every consumed source, header and harness.

These tests execute selected production functions and layouts against mocked
ALSA, PM, clock, register, OF and SoundWire services. They do not establish
physical playback, FIR response, live kernel ALSA/PM/SSR concurrency, DSP
behavior or boot success. Most native analog, interpolator, compander and IIR
paths are preserved rather than independently host-tested. The native FS API is
void and cannot propagate its provider's failure. Borrowed CDC device lookups
still require caller/provider lifetime serialization; a device reference alone
does not retain driver-managed storage. Persistent rollback failures can retain
resources and require parent or hardware recovery. No phone was flashed.
