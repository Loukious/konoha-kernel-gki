# Native LPASS VA macro: frozen handoff

Source and build freeze completed 2026-10-31. This directory supersedes its earlier STATE.md drafts. Do not edit frozen-source or the patch after integration. Root owns central integration, combined verification, commits and pushes.

## Scope and integration

- Exactly one native C unit/input: `qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c`.
- Project: `baseline/kernel/xiaomi/sm8735-modules`; revision `8500748dae32f10507c02f1c94bc971553cb525b`; blob `1693bf4fc50f751d7a1cb967ca09002fdeb31f86`.
- Common remains pinned at `5a54d2ea7de67188274294d346e44a306e36cd03` (6.18.33).
- Private target: `//onyx_audio_va:lpass_cdc_va_macro_dlkm`; integration target: `//onyx:lpass_cdc_va_macro_dlkm`.
- BUILD.fragment already uses central provider labels. Native dependencies remain `wcd_core_dlkm,lpass_cdc_dlkm,swr_ctrl_dlkm`; no exported symbols or OF aliases, matching native. The `va_tx_unmute_delay` parameter remains native.
- Kconfig.fragment/defconfig.fragment add no selections; exact native `CONFIG_LPASS_CDC_VA_MACRO` selection comes from the existing forced sunautoconf.h.
- Reuses frozen CDC, WCD, SPF and SoundWire headers. CDC and all preceding freezes were left unchanged. No TX/RX/WSA/WSA2 input imported.
- **Verifier dependency:** copy `audio_va_mutations.json` beside `verify_va.py`, along with `tests/audio_va_*`. Positive and negative verification load this unique JSON filename. `validation-inputs.json` records its hash.

## Validation

- Final build5: clean, lock-protected, `--jobs=12 --local_resources=memory=20000`; preserved `build.log` and module/config artifacts.
- 109 imports verified against common and actual central CDC/WCD/SoundWire Module.symvers. Exact native module dependency names, absent exports/aliases and parameter preserved.
- 159 cases in 13 groups pass with ASan and UBSan. 34 mutation controls compile and are rejected by assertions/sanitizers.
- Cases cover selected TX/VA DAPM source, divisor preflight, component publication/work drain, wake ownership, controller publication/OF references/notification drain, child bound, PM votes, MCLK rollback, retained temporary TX ownership, double-failure rollback, shared DMIC source ownership, micbias, mixer rollback, rate mapping, SSR reset/timeout and physical-remove/PM ordering.
- Controller platform-data size and all seven offsets are checked against the actual frozen `swr_ctrl_platform_data` layout, including native `handle_irq`/`reg_irq` correspondence.
- Entire native DAI/control/route table block is byte-exact. Native rate/format/register constants, macro callback assignments, PM/OF/platform-driver registration and module parameters are byte-exact. Native 384-kHz hw_params branch remains, while advertised DAI maximum remains 192 kHz.
- Strict checkpatch: zero errors, warnings or checks.
- Exact pinned Git blob/patch replay: zero fuzz and zero offset; dev, staged and frozen sources match.

## Lifecycle and error contracts

Initialize every delayed work item before publishing macro or child callbacks. Close notification admission and drain callbacks before controller unregister; keep callback handle, PM and CDC operations available until controller unregister returns. Do not remove a provider synchronously from a callback being drained.

Component exit cancels delayed work before clearing its component pointer. Physical removal closes/drains/removes children first, then unregisters the macro while CDC cleanup callbacks are still usable, then drains owned PM votes and disables PM. Ordinary component deinit keeps controller children registered. It must not unregister them while retained codec/slave references could wait for card teardown.

Clock ownership commits only after success. Failed MCLK rollback retains a separate owned bare request. SWR retains its temporary TX request when migration release fails; double-failure rollback keeps resource counts and logs the failure. DAPM records the selected TX/VA clock source, shared DMIC records its selected input, and SSR reset ownership is retried rather than forgotten. Final cleanup reports outstanding ownership instead of silently clearing it. Final suspend status changes only after successful suspend.

Decimator writes preserve first errors, skip delayed unmute admission after failed setup and unwind owned shared DMIC requests. Delayed HPF/mute writes log failures and release their wake references. Mixer state rolls back after rejected DAPM updates. All hardware sequences retain native feature controls and route definitions.

## Limits

These are host build, ABI, replay and extracted-production fault tests with mocked ALSA/regmap/clock/PM/regulator/work/platform services. No module-load, phone boot, physical SSR, runtime-PM or real audio success is claimed.

Native FS request and AMIC TX-hold APIs are void. Native ALSA component reads do not expose read status. Provider failures in those interfaces cannot become VA callback status. Persistent hardware cleanup/rollback errors are logged with retained bookkeeping; physical cleanup success under persistent hardware faults is not claimed. Device getters retain the native borrowed-pointer contract; a device reference alone does not retain devres.

## Frozen hashes

- Patch SHA-256: `9a322cb5e7ce91416e379a88dab8504a525bf280da4b0d4a4a45cb69dd829fe2`
- Module SHA-256: `49829b9212751d36f4e1f5b615c23cad14bb3575245b580e7f3cc4fa8c951286`
- Per-source hashes: `final-sha256.json`.

TX is the next natural native macro because its probe defers until VA registers. Its previously discovered sole C blob is `c52ebf738ef769aca11bd1c068690b805f19fca0`; it has not been imported by this handoff.
