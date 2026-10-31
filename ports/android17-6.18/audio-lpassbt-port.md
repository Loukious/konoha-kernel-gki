Native `lpass_bt_swr_dlkm.ko` is frozen and ready for central integration. The exact ROM `.mod` selects one translation unit, `asoc/codecs/lpass-bt-swr.c`, pinned at modules revision `8500748dae32f10507c02f1c94bc971553cb525b`. Its six audio-header dependencies were already frozen. No new alternate parent, codec, BT consumer, common, baseline, central or earlier frozen source was edited.

The normal 12-job / 20000-memory build2 passed without compiler/modpost warnings or errors. All 43 imported CRCs match the pinned GKI and real SPF, WCD core, snd_event and SoundWire controller providers. These four native dependencies, both native OF aliases, zero native exports, and native `pre: bt_fm_swr` softdep are preserved. The forced native sunautoconf header still selects CONFIG_LPASS_BT_SWR, and native PM/PM_SLEEP choices are verified. The parent platform-data structure remains byte-identical to the original; tests also check its size and IRQ-field offset against the frozen controller's actual platform-data structure. `handle_irq` remains NULL, so the controller uses its own IRQs.

Changes:

- Create the single SoundWire controller child synchronously after parent clocks, callbacks and PM state are initialized. Reject missing or duplicate matching children before publishing a device. Acquire an independent OF-node reference for the child, balance failed allocation/add paths, and use explicit platform-device unregister. The asynchronous creation work, reallocating controller list and incorrect put-only cleanup are removed.
- Register sound-event callbacks after child publication. Propagate registration and initial-notification errors, and unwind the child/parent. Cleanup first deregisters from snd_event, whose frozen implementation synchronously drains callbacks, then unregisters the controller while parent PM, clocks and storage remain alive. Only after the controller has drained its calls does cleanup disable runtime PM and release remaining clocks/pins/hardware votes. Driver-data publication is withdrawn last.
- Return PM-resume failures and balance a successful PM reference when hardware-vote validation fails. Runtime resume returns hardware-vote errors and unwinds an acquired core vote when the audio vote fails; runtime suspend releases only recorded votes without underflow. The source's unused global parent pointer is removed.
- Preserve clock/pinctrl failure codes, balance PM references on all clock-enable paths, unwind the first MCLK if the second fails, track whether physical clocks are enabled, and reject clock-user overflow and unmatched disables. Missing optional clocks remain supported; real optional/required clock acquisition errors, including probe deferral, propagate.
- Serialize SSR transitions and use explicit concurrent dev_up accesses. Failed pin/clock restoration leaves the parent down and returns the error instead of notifying the child of false readiness. Retry does not double-disable clocks. A down transition before initial enable no longer loses the later up transition.
- Balance the pinctrl phandle through devres, adapt removal to 6.18's void callback, make private helpers static, and return module driver-registration failures.

Validation:

`verify_lpassbt.py WORKSPACE --source-dir onyx --output FILE` compiles the complete actual parent source and the actual frozen controller platform-data declaration with ASan/UBSan. Eight groups pass 44 cases: all six probe acquisition/publication failures, all four clock lookup deferrals, pinctrl deferral, initial-event failure, absent/duplicate child nodes and retained OF references, both hardware-vote failures, PM failures, MCLK failures and balanced multi-user clocks, overflow/unmatched disables, failed and retried SSR recovery, pthread removal waiting for an in-flight SSR callback, absent optional clocks, post-resume vote validation, and module-registration failure.

`--negative` compiles and rejects eleven regressions: missing child bounds, a borrowed child-node reference, missing child unregister, missing SSR deregistration, disabling parent PM before controller teardown, missing failed-vote cleanup, masking runtime-resume errors, missing PM puts, leaking the first MCLK, masking SSR restoration errors and masking module-init failure. Host DT/devres/platform-driver/PM/clock/pinctrl/sound-event services are mocks; the child mock calls the production parent callbacks during unregister and checks the parent remains usable. These tests do not execute the actual Linux controller probe, PM scheduler, sound-event framework, physical SSR or BT bus.

Strict checkpatch is 0 errors / 0 warnings / 0 checks. The one pinned blob verifies; zero-fuzz/offset patch replay matches development, staged and frozen source bytes. Source, controller-header and harness hashes are bound by the reports.

Patch SHA256: `679a81c78abad8cb1e40c1c0812f9674fa7d450417d9166da6ddf9214c4deec6`.
Module SHA256: `3aa8983568ab57a6b883b33cca213895a85f5bec991a24b5f9b6821372057a6c`.

Integration inputs: `imports.json`, `modules.json`, `BUILD.fragment`, `Kconfig.fragment`, `defconfig.fragment`, `patch`, `tests/`, `verify_lpassbt.py`, reports and `frozen-source/`. Convert standalone `//onyx:` dependency and forced-include labels to local labels. This group adds no header target.

Limits and next dependencies:

- Controller removal may wait for retained SoundWire device/master references, as documented by the frozen bus/controller. Consumers must drop those references; the parent intentionally remains alive while waiting. Parent removal must not run synchronously from its own SSR/controller callback.
- Normal callback enables/disables must remain balanced. Physical clock/pinctrl rollback can itself fail; no hardware rollback success is claimed. The native runtime-PM hierarchy and direct-controller IRQ arrangement are retained.
- No phone boot, Bluetooth transport/audio, actual runtime suspend, hardware clocks, physical SSR, or Linux driver-core removal was validated.
- LPASS CDC core is the next parent branch. Its native dependencies include `typec_analog_acc_dlkm`, which is not yet ported; the other immediate providers are SPF, snd_event and existing qti-regmap-debugfs. Native LPASS macros and dependent codecs follow those providers.
