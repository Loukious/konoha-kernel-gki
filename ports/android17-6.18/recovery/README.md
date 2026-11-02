# Recovery init mode correction

The serial module diagnostic package sets `androidboot.init_rc` to a script
on metadata for normal Android startup. Recovery skips normal first-stage
mounts. Its init therefore attempted the metadata override before the script
was accessible, bypassing the native recovery scripts with no parser fallback.

The source patch checks the existing diagnostic flag in the `RECOVERY` build
and clears that normal-boot override before native script selection. Normal
Android and explicitly configured recovery overrides without the diagnostic
flag retain their existing behavior. No files are added under metadata and no
new bootstrap, init service, module loader, mount or kernel change is introduced.

Pinned Android core revision: ee96af1583278d5c4a32b7ddc6665d0e81e04090.
The source patch is separate from platform kernel patches, which `stage.py`
applies to Onyx driver imports.

Reproducible evidence and build commands are in
`../validation/recovery_init_override/`. Original compiler/link/strip settings
are recovered from the local Soong graph for the exact `armv8-2a-dotprod`
recovery variant. Relinking and stripping the unchanged objects reproduces the
backup's init byte for byte. Candidate builds replace only init.cpp's archive
member, and preserve the existing shared-library dependencies.

`build_recovery_init_fix.py` repacks the verified original recovery. Its audit
requires that only system/bin/init changes, all other raw CPIO records and
paths remain identical, image headers and the AVB key are preserved, and the
image fits the exact original recovery partition. Source selection tests run
both normal and recovery builds under ASan/UBSan and reject the original bug.
The native recovery RC directory also passes Android host_init_verifier.

Offline validation does not prove that recovery/fastbootd works on hardware.
The prepared live test flashes recovery_a only, tests fastbootd, verifies six
partition readbacks including all five unchanged normal-boot images, and
returns to the installed 6.18 Android build. A physical recovery bridge back
to the same matched 6.18 images is prepared as the rescue path.
