# Native LPASS TX macro: frozen handoff

Frozen 2026-10-31. Root owns central integration, combined verification, commits and pushes. Preserve the patch and frozen-source after integration.

## Exact scope and integration

- One native C unit/input: `qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-tx-macro.c`.
- Project `baseline/kernel/xiaomi/sm8735-modules`; revision `8500748dae32f10507c02f1c94bc971553cb525b`; blob `c52ebf738ef769aca11bd1c068690b805f19fca0`.
- Common pin remains `5a54d2ea7de67188274294d346e44a306e36cd03`, release 6.18.33.
- Private target `//onyx_audio_tx:lpass_cdc_tx_macro_dlkm`; integration target `//onyx:lpass_cdc_tx_macro_dlkm`.
- BUILD.fragment already uses central header/provider labels. Native sole dependency `lpass_cdc_dlkm`; sole export `lpass_cdc_tx_macro_mute_hs`; no aliases; native `tx_unmute_delay` parameter retained.
- Native forced `CONFIG_LPASS_CDC_TX_MACRO` comes from the existing sunautoconf.h. Fragment files add no selections. Probe still waits for VA registration.
- All consumed headers are reused from frozen providers. No CDC/VA/SoundWire source changed. No RX/WSA/WSA2 source imported by this handoff.
- **Verifier dependency:** copy `audio_tx_mutations.json` beside `verify_tx.py`, plus `tests/audio_tx_*`. The unique JSON dependency is required for both positive and negative runs. `validation-inputs.json` records hashes.

## Validation

- Final build2 passed without compiler warnings/errors, under the shared build lock, with 12 jobs and 20,000 memory resource units. Build log, config and module are preserved.
- 87 imported CRCs verified against actual common and central CDC Module.symvers; native dependency, export, aliases and parameter match.
- 184 ASan/UBSan cases across 15 groups; 37 deliberately broken variants compile and are rejected by assertions/sanitizers.
- Coverage includes clock/PM/register/FS rollback, DAPM ownership, component init/unwind, global headset callback drain, repeated headset requests and original-state restoration, per-DAI decimator ownership, concurrent headset/stream serialization, work wake references, shared DMIC source retention, BCS channels/control bounds, enum errors, native rate mapping, SSR status, teardown and final suspend failure.
- Native DAI/control/route table block is byte-exact. Feature constants and preprocessor branches, platform/PM/OF registration, macro callback assignments and native micbias no-op are unchanged.
- Native three DAIs retain eight capture channels each and 8/16/32/48/96/192-kHz advertised rates. Native hw_params 384-kHz branch remains.
- Strict checkpatch reports zero errors, warnings or checks.
- Exact pinned Git blob and patch replay with zero fuzz/offset; dev, staged and frozen sources match.

## Shared contracts and native configuration

All work initializes before macro registration. The exported headset-mute API publishes its global pointer only after component initialization. Removal closes admission, drains active exported calls, cancels every delayed job and then clears the component pointer. Do not remove the provider synchronously from a callback being drained. Native device getters remain borrowed; a device reference alone does not preserve devres.

MCLK/FS and first/last control-register sequencing retain native order. Successful operations commit counts; failed rollback retains a separate owned clock request. Component cleanup releases only its own tracked DAPM clock and DMIC requests; physical cleanup handles remaining clock requests while CDC operations and PM remain live. PM status changes only after successful final suspend.

Each stream owns its saved decimator mask, so repeated unmute does not duplicate references and mixer changes cannot make mute release another stream's channels. Headset mute retains each channel's original mute state across repeated calls and restores its owned mask. The shared stream mutex serializes those register/state changes. Failed setup skips delayed unmute admission and retains or unwinds resource ownership.

**Effective native delay is 40 ms.** The C source selects 10 ms only if `CONFIG_TARGET_PRODUCT_ONYX` is defined. That symbol is absent from the actual ROM .config, forced sunautoconf.h and native compile defines, and remains absent from the new config. The host harness preserves the conditional and checks the resulting 40-ms delay. The native `tx_unmute_delay` module parameter is retained, but the native scheduling path does not consume it.

## Limits

These are host build/ABI/replay checks and extracted-production fault tests using mocked ALSA, regmap, clock, PM, work and platform services. No module load, phone boot, physical SSR, runtime-PM or real audio success is claimed.

Native FS and AMIC TX-hold APIs are void, and ALSA component reads do not expose read errors. Provider failures in those interfaces cannot become TX callback status. Persistent hardware rollback failures are logged with retained bookkeeping; successful hardware cleanup under persistent faults is not claimed.

## Frozen hashes

- Patch SHA-256: `b802424859043e542cfe732c2c24d06c5e9df0e6f847ed81a8433afaf7bfff50`
- Module SHA-256: `10662e531c2aa4006afd775276b060ec4c5277f8fe8f3aa0e90d282399da0a2a`
- Per-source hashes: `final-sha256.json`.

Next selected native scope is RX; discovery is recorded separately in `/tmp/onyx-audio-rx-discovery.json`.
