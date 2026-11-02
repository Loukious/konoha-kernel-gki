This handoff ports six native single-object audio modules: LPI pinctrl, the SDCA
register debugfs service, SoundWire DMIC, SoundWire haptics, the native hardware
parameter stub codec, and HDMI/DisplayPort audio. Ten fresh pinned source/header
paths are owned here. WCD939x and SoundWire provider/header inputs are consumed
from their owners; no shared or central source was edited.

The original compiled `.mod` and `.cmd` receipts and module metadata are recorded
per module. All source pins match the native ROM build. The native DAI, route,
control, register and string tables remain intact, including both stub branches;
the selected AW882XX configuration advertises 8 channels through 96 kHz. DMIC
keeps its native WCD939x supply/notifier branch. `sunautoconf.h` is forced for
every native unit, with no warning or KMI bypass.

LPI now stores its own pinctrl descriptor and uses public APIs instead of
reading the old private kernel structure. Its unused native private header
inputs remain pinned for provenance. GPIO callbacks may sleep; pin and register
offsets are bounded; multiple pin configurations retain their loop index; PM
errors and registration unwind are balanced. SDCA reads return formatted bytes
only, support short reads and valid file positions, propagate write failures,
and clean incomplete debugfs creation. SDCA callers must remove entries before
freeing the borrowed component/regdump storage.

DMIC retains its supplier device and a managed dependency link, balances OF
references and notifier registration, and releases failed probes without
unregistering the SoundWire device from its own callback. Micbias votes advance
only after successful enable/disable. Haptics drains both notifier sources and
the ASoC component before power/storage release and unwinds every probe exit.
HDMI balances parent references and withdraws its registered operation table
before freeing it; it requires the separate frozen extdisp provider extension.

Integrate `imports.json`, `patch`, the build/config fragments and `modules.json`
through the central recipe. Apply the SoundWire IRQ and extdisp provider patches
first, and include WCD939x's real codec/slave provider. `integration-dependencies.json`
maps standalone labels to central targets. The new public
`audio_small_headers` target also supplies SDCA's unchanged header to WCD9378.

`verify_small.py --source-root WORKSPACE/onyx --negative` runs production-source
host tests after integration. They exercise 8 LPI, 14 DMIC and 12 haptics probe
failure paths, 36 SDCA short-read chunk sizes, refcounts, notifier cleanup,
micbias/PM errors, pin bounds and multi-configuration behavior. Seventeen
deliberate regressions must fail compiled ASan/UBSan tests. These fixtures
simulate kernel services; they do not prove real notifier, driver-link, ASoC,
debugfs proxy, SoundWire, clock or DSP timing.

`verify_artifacts.py --workspace WORKSPACE --package onyx` checks native units,
tables, selected features, aliases/exports/parameters/dependencies, vermagic and
every imported CRC against the final providers. `validation.json` records the
frozen result, exact source/shared-header/build/config hashes and zero-offset
patch replay. Captured `.ko` files are unstripped build evidence. Root owns the
combined module/image packaging and runtime validation.

`checkpatch.log` retains the raw strict result. The unsigned handoff has no
source-style warnings/checks; its only administrative error is missing commit
signoff. `checkpatch-code.log` additionally checks with only signoff disabled.
The integrator owns any commit signoff.

Both checked Onyx DT variants enable LPI, the native stub and HDMI. The haptics
node is disabled; no DMIC node matches; SDCA has no direct node. These ROM
alternatives are preserved without claiming active hardware. No phone flash,
boot, playback, capture, haptics or physical display-audio test is claimed.
