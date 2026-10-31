# Native MBHC lifecycle port

The MBHC changes preserve the native two compilation units, exported function
signatures, register definitions, calibration formats, detection algorithms and
selected ADC/USBSS/FSA features. `CONFIG_AUDIO_QGKI` remains disabled in the
recorded native build; calibration upload through the hwdep interface is not
claimed. The LPASS include uses the same existing pinned header through its
DDK-exported include directory.

Initialization creates both jacks, synchronization objects, delayed work,
detection callbacks and extcon state before registering notifiers or interrupts.
All seven native IRQ entry paths reject work before start. Failed extcon setup
retains the actual error, and a retry completes a partially created pair of
jacks. Failure paths withdraw only registered notifiers and acquired IRQs and
release their device-tree references. A completed deinit can be followed by
native SSR reinitialization with the retained ALSA/extcon objects.

The started flag publishes the callback configuration with release/acquire
ordering. Stop closes the work-enqueue gate, drains both notifier sources and
unconditionally cancels firmware work before waiting for detection/button work.
It restores the caller's original calibration pointer before releasing loaded
firmware. GPIO phandles acquired by start are tracked separately from borrowed
configuration fields. Deinit additionally releases IRQs and provider phandles
before destroying the lock. Stop/deinit are safe after successful init without
a start, and repeated stop/deinit do not disable IRQs repeatedly or release a
reference twice.

A repeated start with the same configuration is idempotent; replacing an active
configuration returns `-EBUSY`. Native USBSS registration can fail after linking
its notifier, so the error path explicitly attempts withdrawal. The one native
Type-C notifier cannot belong to two lists at once; a configuration specifying
both provider handles is rejected. ADC detection scheduling balances its suspend
vote when acquisition fails or the work is already queued.

The optional `skip_imped_detect` callback retains its native unlocked execution
before waiting for work. IRQ disable uses the existing provider callback, whose
frozen WCD implementation calls `disable_irq_nosync`. Debugfs keeps its native
path, operations and mode and is now created once at module initialization and
removed on module exit.

## Validation

`mbhc-lifecycle-test.py` extracts the real publication helpers and the real
init/start/stop/deinit, notifier-withdrawal, GPIO lookup, firmware-worker,
mechanical-IRQ and ADC schedule/cancel bodies from the selected source root.
Twenty-six scenarios pass with AddressSanitizer and UndefinedBehaviorSanitizer.
They cover fourteen distinct setup failures followed by retry, missing detection
support and invalid inputs, duplicate lifecycle calls, both Type-C providers,
partial notifier registration, pending firmware without a published pointer,
late firmware completion, original-calibration restoration, suspend-vote
accounting and the unlocked cancellation notification.

Eight compiled deliberate regressions are rejected: conditional firmware
cancellation, a missing enqueue gate, a lost extcon error, a notifier leaked by
failed initialization, missing USBSS error withdrawal, missing calibration
restoration, an extra suspend vote and a mechanical IRQ admitted before start.
The JSON report records the function, source and harness hashes and individual
rejecting diagnostics. Run the same verifier against the final staged tree.

Strict checkpatch on `mbhc-source.patch` reports zero errors, one warning and
zero checks. The warning is the existing native debugfs mode `0666`, retained
while moving creation into module initialization. `mbhc-final-sha256.json`
identifies the final three changed inputs. Root owns final DDK compilation,
source-pin/patch replay, ABI verification and combined archive integration.

## Consumer contracts and limits

The codec owner must serialize init/start/stop/deinit and keep its MBHC object,
component, callback table and caller calibration alive for that sequence. It
must call deinit before releasing the codec or its IRQ domain. These lifecycle
functions must run from process context outside their own notifier callbacks.
No safe concurrent restart/reconfiguration by independent owners is provided.

The host services model IRQ registration, work cancellation, blocking-notifier
withdrawal, ALSA/extcon, device-tree references and firmware allocation. Most IRQ
handlers and the analog/detection hardware helpers are mocked; only the
mechanical IRQ entry and ADC scheduling/cancellation execute in this harness.
The tests do not run the complete kernel, arbitrary hardware concurrency or
real IRQ/DMA, SoundWire, DSP/SSR, headset and button behavior.

Notifier safety depends on the providers draining callbacks during withdrawal
and removal. The native core unregister callback and frozen Type-C providers
supply that contract; this consumer cannot establish physical provider removal
or recovery if an external implementation retains a notifier after an error.
Hardware shutdown and long firmware-request latency remain subject to the
native provider/firmware behavior. The whole port still requires phone testing.
