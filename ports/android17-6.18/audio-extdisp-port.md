The display provider can now withdraw its caller-owned codec operation table
before the HDMI component frees that storage. The new export checks the exact
registered pointer; retries after removal succeed and stale pointers cannot
remove a replacement. Existing exported signatures, CRCs, public structures and
permanent provider binding remain unchanged.

Integrate `patch` before the small-audio patch. It applies to the two **existing
integrated** source/header files, whose base hashes are in
`original-sha256.json`. There are no new source imports or module names. The
standalone `BUILD.fragment` belongs to `//onyx_extdisp_audio`; the central
`msm_ext_display` target already has the required source/header wiring.

All provider accesses to the registered table use its publication mutex. Ready
prepares a callback snapshot before unlocking, so an in-flight ready callback
can safely finish after withdrawal and can withdraw itself. Synchronous extcon
callbacks already hold publication; a private current-task marker lets that
same task withdraw directly. Other tasks acquire the mutex and drain existing
table accesses. The caller must still quiesce its own uses before freeing.

`verify_extdisp.py --negative --source-root WORKSPACE/onyx
--consumer-root WORKSPACE/onyx` rechecks production provider and HDMI lifecycle
functions after integration. Tests cover identity/retries, concurrent table
writes, both self-withdrawal contexts, detached ready completion after free,
four consumer probe failures, retained device/node references, and failed
withdrawal followed by retry. Seven deliberate regressions are rejected by
compiled ASan/UBSan fixtures. Kernel and hardware services are simulated.

`validation.json` records artifact CRCs, preserved exports, source/config/build
hashes, exact patch replay, test inputs and limits. `checkpatch.log` is the raw
strict check, including the unsigned handoff's missing signoff. The additional
`checkpatch-code.log` passes with only signoff checking disabled; it filters no
source diagnostics. The integrator owns any commit signoff.

Frozen sources and artifacts are evidence, not a phone-tested release. Physical
audio, real ASoC/extcon ordering and phone boot remain unvalidated.
