# Native LPASS WSA and WSA2 macros

Both speaker macros retain their exact ROM-linked compilation units, register,
rate, mixer, route and DAI tables and the native feature selection. They use the
existing LPASS CDC, WCD core and SoundWire controller providers. Both native
export, module-alias and parameter sets are empty and remain unchanged.

The port adapts the const DAI channel-map and void platform-remove APIs. Selected
child devices are created synchronously with bounded allocation and balanced
OF/device references; published controller targets drain notifications before
removal. Cooling registration has explicit ownership, and component removal
withdraws the component pointer and drains cooling work. WSA's delayed VI worker
is initialized during probe and cancelled before component release; WSA2 retains
its native synchronous VI path. Probe checks mappings and DT-array lengths,
retains GPIO phandles, and initializes PM before macro publication.

Four source-function sanitizer suites cover setup rollback, notification
reentry, component/cooling lifetime and DT-array parsing, including 400 controlled
concurrency cases. All 33 compiled deliberate regressions are rejected. Native
tables, generated compilation units/configurations, dependencies, source replay
and 215 imported-symbol CRCs were verified in the isolated build. Strict
checkpatch reports no errors, warnings or checks. The combined build has its
own module verification report.

The [validation directory](validation/audio_wsa/) stores the native evidence,
frozen hashes, reports and test fixtures. Run the integrated checks after staging:

```sh
python3 ports/android17-6.18/validation/audio_wsa/verify_children.py --source-root WORKSPACE/onyx --negative --output wsa-children.json
python3 ports/android17-6.18/validation/audio_wsa/verify_lifecycle.py --source-root WORKSPACE/onyx --negative --output wsa-lifecycle.json
```

The tests use mocked OF, platform, SoundWire, register and workqueue services.
They do not validate physical speakers, amplifier/machine-driver integration,
firmware/DSP, PM/clock operation, playback or SSR. ASoC/CDC must quiesce DAI and
macro calls before component teardown; SoundWire consumers must release retained
references before controller removal. Native clock/register paths outside the
changed lifecycle retain their existing behavior, including the void FS-request
API's inability to report provider failure.
