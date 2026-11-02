# Native AW882xx and SIA91xx amplifiers

Three modules retain all 15 native compilation units, native dependencies,
device aliases, parameters and exported-symbol names. Thirty pinned source and
header inputs replay exactly through patch 0131. Firmware size, lists, nested
records and CRC checks reject malformed data. Tuning messages use a copied,
validated header and bounded queues. Registration failures unwind owned state;
component removal drains work and IRQ ownership before freeing resources.

Integrated production-function tests pass 250 cases with ASan/UBSan and reject
18 compiled regressions. Strict builds and imported-symbol CRC checks pass.
Checkpatch reports zero errors, four uncommented acquire-load advisories and
20 style checks; the complete report is in validation/audio_amplifiers/.

Archive artifacts/20261101T111342557781Z contains 307 platform modules and passes 19,909
imported-symbol CRC checks. The GKI Image, GKI exports and all previous platform
symbol tables match the 304-module checkpoint. Device-tree/overlay checks pass.
Vendor-name coverage is 304/466 (65.2%); 162 names remain to assess or port.

These checks do not establish physical audio, amplifier firmware compatibility,
concurrent hot-unbind, phone boot, or complete kernel/DSP/SSR behavior. Matching
boot/DLKM packaging, KernelSU/NetHunter and phone validation remain unfinished.
