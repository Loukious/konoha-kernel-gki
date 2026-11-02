Native WSA883x and WSA884x codecs are ready for central integration. This handoff
is frozen; do not rerun adaptation, stage, or freeze scripts. The separate
completed LPASS macro handoff remains untouched.

Each module retains its three native source units, all public headers, register
descriptions, tables, DAI formats/rates, six exported names, and native module
dependencies. Fifteen fresh pins are listed in imports.json. The exact unchanged
bolero-slave-internal.h pin is owned here and exported by
//onyx_audio_speakers:audio_wsa_codec_headers for WCD and machine consumers.

The patch checks acquisition failures, initializes locks before callbacks can
run, records requested IRQs, and withdraws/drains SoundWire IRQ publication before
component/IRQ/domain teardown. It uses the correct handler cookies, devm-owned
driver names, balanced node/device references, and a parent supplier link. Proc
and local debugfs interfaces publish after fallible setup. Supply init/enable
failures unwind with their original error; failed probes no longer remove their
own bus device. WSA884x DT gain and port arrays require exact sizes, gain/rload/
battery values are checked before narrowing/indexing, and register dumps handle
short buffers and EOF without overwriting memory or losing position on copy
failure. Native successful hardware register sequences remain in place.

Validation: clean build4, strict checkpatch 0 errors/0 warnings/0 checks, 195
imported CRC comparisons, native module interfaces/dependencies, 49 unchanged
native tables, shared/provider header hashes, and exact zero-fuzz/offset replay
from pinned Git blobs. The only generated extra object is inspected DDK
built_with metadata. The host suite executes production probe, cleanup, supply,
GPIO, register-dump and WSA884x port parsing functions: 186 scenarios, including
93 probe failpoints, with ASan/UBSan. Fourteen compiled regressions are rejected.

Revalidation after integration can use:

    python3 verify_probe.py --source-root <central-staged-source> --negative --output <report.json>

No phone or live-kernel testing is claimed. Parent notifier and SoundWire IRQ
withdrawal rely on the driver core's serialized probe/remove context and are
never called from nested IRQ callbacks. Host mocks do not validate supplier
unbind races, ASoC card rebind, physical IRQs, DSP/SSR or hardware error recovery.
Onyx's current DT disables the WSA codec alternatives and enables AW/SIA
candidates; actual populated amplifier-family detection is still needed.

Provider follow-up: source/patch freeze is final, but build4 artifact hashes and
provider CRC evidence are provisional pending the SoundWire IRQ owner's final
CRC-preserving snapshot. A subsequent artifact verification record will
supersede build4; frozen source must not be re-adapted.
