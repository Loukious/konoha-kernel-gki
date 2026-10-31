Native `typec_analog_acc_dlkm.ko` is frozen and ready for integration. The ROM selects one translation unit, `asoc/codecs/typec_analog_acc.c`, plus its local header; both exact pins come from modules revision `8500748dae32f10507c02f1c94bc971553cb525b`. Its real dependency remains `ucsi_qti_glink`. No common, baseline, central, earlier frozen provider, LPASS CDC or codec source was modified.

The normal 12-job / 20000-memory build2 passed cleanly. All 15 imported CRCs match GKI and the real frozen UCSI provider. The native `typec_analog_acc_init` export, empty aliases and sole native dependency are preserved. One real cleanup export is added: **`typec_analog_acc_exit(struct snd_soc_component *)`**. The upcoming LPASS CDC consumer must call it before component/card teardown. The forced native sunautoconf selection remains CONFIG_SND_SOC_TYPEC_ANALOG_ACC; CONFIG_UCSI_QTI_GLINK=m and native ALSA/Type-C kernel choices are verified.

The native code published its UCSI notifier before initializing the jack, dereferenced failed allocations, allowed global instance replacement, and freed notifier storage at module exit without unregistering. The port initializes jack state before registration can deliver events, handles allocation/jack/notifier failures with complete cleanup, and rejects a second active instance with -EBUSY. Successful initialization owns one balanced provider module reference until cleanup, so normal unload cannot free an active notifier. Instance-specific exit ignores unrelated components, synchronously unregisters/drains UCSI callbacks, frees the ALSA jack and private state, then drops that reference. Registration/unregistration use the frozen provider's actual blocking-notifier semantics. Report serialization protects cached accessory state against overlapping notifier calls.

Native event behavior is preserved: AUDIO reports `SND_JACK_HEADSET | SND_JACK_VIDEOOUT`, NONE clears those bits, duplicate states do not generate duplicate reports, and other accessory kinds do not change the jack. NULL payloads are ignored. The real UCSI payload header remains unchanged.

Validation:

`verify_typec.py WORKSPACE --source-dir onyx --output FILE` compiles the full actual provider source and actual frozen UCSI header with ASan/UBSan. Five groups pass 21 cases: invalid/unavailable-module/allocation/jack/notifier failures, an immediate event during registration, duplicate and foreign-component lifecycle calls, reinitialization after exit, native and ignored event states, pthread teardown waiting for a blocked report, and concurrent duplicate-event serialization.

`--negative` compiles and rejects ten regressions: ignoring allocation failure, replacing an active instance, missing module ownership, missing notifier unregister/drain, cleaning the wrong component, missing jack cleanup, missing event serialization, missing NULL-payload check, changing the native report mask, and notifier publication before jack creation. Allocation, module, ALSA jack/card and notifier services are mocked. These are source/lifetime tests, not real ALSA-card or UCSI hardware tests.

Strict checkpatch is 0 errors / 0 warnings / 0 checks. Both exact pins verify, and zero-fuzz/offset replay matches development, staged and frozen bytes. Final reports bind the production source/header, UCSI header and harness hashes.

Patch SHA256: `4ece3bf5c021c26e42ee21099874a689d8db16018183548be0e90246d73d379b`.
Module SHA256: `5e8d633c70b37227a5a992b6f8dd7261ad7068b4c2a3f133b68b9b86c31003b5`.

Integration inputs: `imports.json`, `modules.json`, `BUILD.fragment`, `Kconfig.fragment`, `defconfig.fragment`, `patch`, `tests/typec.c`, `verify_typec.py`, reports and `frozen-source/`. New header target: `audio_typec_headers`. Convert standalone `//onyx:` labels, including the forced include, to local labels.

Required consumer lifecycle:

- A successful init requires a matching `typec_analog_acc_exit(component)` before the component or its ALSA card is destroyed. Exit must run outside UCSI/jack callbacks because unregister waits for callbacks. The forthcoming LPASS CDC port must call it in component removal and any post-init failure unwind.
- Device removal alone is too late if the ALSA card/component is already gone, so there is deliberately no device-devres fallback. A missed cleanup keeps the provider pinned instead of permitting module text to disappear; that pin does not retain an ALSA card. Proper consumer cleanup remains mandatory.
- The native singleton design is retained with explicit duplicate rejection. No multi-card support or new initial accessory-state query is introduced.
- No actual ALSA card registration/removal, UCSI transport, USB accessory detection, headset operation, audio playback or phone boot was validated. LPASS CDC source has not yet been imported; its native registration gate and all component failure paths still need review.
