# Native LPASS WSA and WSA2 handoff

Both native speaker macros are ready for central integration. Build5 succeeds
with no compiler/modpost warnings; strict checkpatch reports 0 errors, 0 warnings,
and 0 checks. `validation.json` records 215 imported-symbol CRC checks against
the frozen GKI, LPASS CDC, WCD core, and SoundWire controller providers.

The two modules preserve the exact ROM-linked compilation units, native
register/rate/control/route/DAI/driver tables, feature choices, and dependency
set. Both native modules have no exports, module aliases, or parameters; the
ported modules retain those sets. The native device match tables remain intact.

## Changes

- Adapt channel-map callbacks to the const DAI API and platform removal to void.
- Create selected child devices synchronously with bounded allocation and exact
  OF/device reference rollback. Publish controller targets after successful setup,
  and drain reentrant notifications before unregistering their targets.
- Pair explicit thermal registration with explicit removal. Cooling register I/O
  holds a component mutex; component teardown clears the pointer and cancels work.
  Initialize the WSA delayed VI worker during probe and cancel it before component
  release. WSA2 retains its native synchronous VI path.
- Check optional register mappings and exact DT-array lengths/read errors, hold
  GPIO phandles through managed cleanup, set up PM before macro publication, and
  unwind failed publication. Parse noise-gate settings before component creation;
  successful probe no longer returns a stale optional-property error.

## Integration inputs

`imports.json` has four fresh files from baseline module revision
`8500748dae32f10507c02f1c94bc971553cb525b`: the two C files and their two private
headers. `units.json` records one compilation unit per module. `shared-headers.json`
and `shared-header-owners.json` record the nine existing audio headers and owners.
All central, common, baseline, and previously frozen source files remain unchanged.

Use `patch`, `BUILD.fragment`, `modules.json`, `Kconfig.fragment`, and
`defconfig.fragment`. The config fragments only document the existing native
`sunautoconf.h` selection. Convert `//onyx:` labels to local labels when merging
the build fragment into the central package. The module providers for both are
`lpass_cdc_dlkm`, `wcd_core_dlkm`, and `swr_ctrl_dlkm`.

`frozen-source` and `frozen-hashes.json` are the verified immutable result.
`freeze.py` has already verified zero-fuzz/zero-offset replay, source and header
hashes, native tables, generated Kbuild units and configurations, module metadata,
and the final tests. `build.log` and `checkpatch.txt` preserve the successful logs.

## Repeatable integrated checks

Run the following after central staging. They read the source tree and compile
host test executables in temporary directories; they do not modify the source.

```sh
python3 /tmp/onyx-audio-wsa-handoff/verify_children.py \
  --source-root /home/loukious/Android/konoha-android17-6.18-port-20261031/onyx \
  --negative --output /tmp/onyx-wsa-children-integrated.json
python3 /tmp/onyx-audio-wsa-handoff/verify_lifecycle.py \
  --source-root /home/loukious/Android/konoha-android17-6.18-port-20261031/onyx \
  --negative --output /tmp/onyx-wsa-lifecycle-integrated.json
```

The four ASan/UBSan suites execute selected production functions. They cover
every modeled child allocation/add failure, OF/device reference rollback,
reentrant notification withdrawal, thermal states, component access, malformed
DT arrays, and 400 total controlled concurrency cases. All 33 deliberately broken,
compiled variants are rejected. The child harness derives from the existing RX
ownership harness and adds the WSA-specific child filtering cases.

## Limits

These results establish build and selected software-path behavior. Speaker
amplifiers, the machine driver, firmware/DSP interaction, playback, PM/clock
operation, SSR recovery, and the phone have not been validated by this handoff.
ASoC/CDC must quiesce DAI and macro calls before component teardown. SoundWire
consumers must return retained references before controller removal.

The native clock/DSP/register paths outside the changed lifecycle retain their
existing behavior. Persistent provider failures and hardware recovery remain
unvalidated; the native void FS request API cannot report provider failure.
The host tests mock OF, platform devices, register I/O, and workqueue services;
they observe VI cancellation calls without simulating the complete VI worker.
