# Shared clock provider lifecycle overlay

FROZEN for root integration. This updates the existing `clk-qcom` provider and
required Tuna clock drivers used by the compiled Onyx device trees. It adds no
new source imports, modules, or alternative-hardware clock leaves.

Patch SHA256: `03827712cce6e0b66faec9ce563e45c836967934d48aeec783a519ac54350f1f`.
Apply the raw `patch` as a new numbered overlay to the exact preimages recorded in
`replay-verification.json`. Preserve all historical handoffs and frozen snapshots.
Central Kconfig, defconfig, and BUILD targets require no changes. `BUILD.private`
and `stage.py` describe only the isolated verification package, not recipe additions.

The shared owner tracks regulator handles, runtime/ICC/iface votes, proxy votes,
probe PM references, registration groups, and descriptor use. Failed registration
unpublishes and unregisters while power handles remain valid; CCF boot-state holds
are released through `clk_sync_state` first. Clocks are removed from provider and
debug lists, with SRCU draining trace readers. Static init pointers and metadata
are restored after unregistration, and protected/critical publication filtering
uses copied pointer arrays. PM get failures unwind the matching parent reference.

Five runtime Tuna probes use the new owned acquire/release calls and propagate
negative probe-power release and direct setup-register failures. Their native PLL
calls and register offsets/masks/values/order are unchanged. All nine involved
platform drivers suppress manual bind/unbind attributes, including the GPU module's
two drivers. Normal `cleanup_module` entry points remain present.

GCC uses managed DFS setup. Partial setup restores original init/flags/frequency
tables on cleanup. Dynamically derived DFS tables are published only after complete
construction, freed on construction failure, and released by the CCF terminate
callback before their original templates are restored.

A forced teardown with still-live exposed consumers does not reset/reuse their
clock metadata. A final hardware-release fault cannot be rejected by void devres
cleanup: the affected device is quarantined with the recorded error, held device
reference, and actual regulator/ICC handles. The record contains no leaf descriptor
or code pointers, callbacks, workers, or deferred work. Only this exceptional path
pins the shared provider to preserve the refusal registry. Reprobe of that instance
is refused; successful rollback and unrelated devices remain usable. This is not a
claim of recoverable forced hardware removal or of universal driver unload safety.

Validation completed:

- Build7 succeeds without compiler warnings for the provider plus eight existing
  Tuna modules, preserving all 27 native C translation units.
- Exact staged ASan/UBSan production extracts cover provider ownership, allocation
  and registration failures, detached devres callbacks, failed resume/suspend,
  repeated probe/cleanup, exceptional quarantine and descriptor refusal, nested PM,
  every five-leaf setup-write failure, positive successful PM put, DFS lifecycle,
  and concurrent debug reader/removal. Four groups run in two C harnesses.
- All 13 rejecting controls fail as intended. Harness and source hashes are in
  `host-validation.json`. Public descriptor, regmap, VDD and RCG layouts are unchanged.
- Nine rebuilt modules verify 352 imported CRCs. All 139 existing provider exports
  retain their CRCs, with three additive exports. Aliases are unchanged; configs
  match current central target configs byte for byte.
- Replacing provider symvers in the full current catalog verifies 254 modules and
  15,452 imported CRCs. This checks ABI compatibility, not rebuilt runtime behavior
  of every consumer. Root should rebuild the combined recipe after integration.
- Patch replay has zero fuzz and zero offsets; all 16 frozen changed sources match
  the private and staged sources and exact integrated preimages. Checkpatch reports
  zero errors/warnings with commit-message/signoff checks excluded for the raw diff.

For integration, copy the verifier and its support files listed in
`integration-dependencies.json`. The verifier is independent of `original/` and
supports `--package onyx` after integration:

```sh
python3 verify_clock_lifecycle.py WORKSPACE --package onyx \
  --negative-controls --output clock-lifecycle-validation.json
```

Keep `onyx-clock-native-dtb-evidence.json` in the control repository: the compiled
`tuna.dtb` and `tuna7.dtb` select all required Tuna/GX clock nodes, and neither those
whole trees nor the compiled Onyx overlay has any `opp-handle` property. Native OPP
code remains unchanged and enabled; generic OPP ownership outside these recorded
DTs is not newly established. Sun/Kera clock modules are alternative-hardware ROM
names, not missing clock controllers selected by these final Onyx trees. Root owns
its separate three video clock-binding header imports; this handoff duplicates none.

The native PLL configure APIs still return void. Host tests model kernel dependencies
and do not establish hardware bootability or actual CCF/PM scheduler behavior.
