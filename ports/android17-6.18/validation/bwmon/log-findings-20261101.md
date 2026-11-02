# Camera preview performance investigation

User confirms camera and fingerprint work after patches0150/0151. Rear live preview remains choppy. Recorded HAL sample median20.87fps over21 samples, requested30fps;19–22fps occurs even with8.3–16.7ms exposure. Processing and updateTexImage stalls observed. These establish slow pipeline timing, not a camera driver root cause.

On6.18 both native LLCC and DDR BWMON platform devices are present but unbound. PowerHAL cannot open six real LLCC tunables;15927 failed writes in the recorded log through23:46:04. Native6.6 comparison contains zero such failures. Patch0154 restores pinned native BWMON with OF aliases and boot-lifetime registration. Driver binding, initialization before PowerHAL opens its nodes and performance improvement require hardware validation.

Display initialization emits three refcount warnings with old raw bridge allocation. Display kickoff also emits usleep_range_state warning from unsigned subtraction for sub-millisecond waits. Patches0152/0153 address these independently; their relationship to preview lag remains unproven. Exact-source offline checks pass.

Other issues remain: charger init retry5251 occurrences over the current roughly22min recording versus one occurrence during the shorter native boot recording, matching charging UNKNOWN. This is an unresolved initialization problem; raw counts from different recording durations are not a normalized comparison. Audio NULL mutex appears in both kernels (1319 current,78 native); no new kernel root cause is established. ICP/SYNX warnings also occur in native6.6. Camera provider cgroups currently cpu/cpuset camera-daemon despite vendor warning.

Sources: current camera-fp-20261101T222213Z/live-618/logcat-all.txt and native usb-ipa-interconnect-20261101T205625Z/baseline-66-live/logcat-all.txt under the device backup. Detailed reports copied into the current attempt. No claim that preview performance is fixed before comparable live capture.
