# Current Onyx 6.18 checkpoint

Use `/home/loukious/Android/konoha-6.18-port-control` on `port/android17-6.18-onyx`.
The 6.18 build workspace is `/home/loukious/Android/konoha-android17-6.18-port-20261031`.
The original `konoha-kernel-gki` checkout remains the stable 6.6 tree.

Latest successful archive: `artifacts/20261031T232938953896Z`.
288 platform modules; 18,369 imported CRCs; DT/overlay checks pass with the
existing 1,084 vendor warnings. 285/466 ROM module names (61.2%) are covered;
181 remain, including alternative hardware. No phone flash or boot is claimed.
After this build, one extra terminal blank line was removed from the recipe's
Kconfig; no configuration entries or driver source changed. The archive retains
the exact Kconfig text consumed by its build.

This checkpoint integrates native WCD9xxx/MBHC (2 modules), WSA/WSA2 (2) and
RMNET extensions (7). Their isolated and integrated evidence, verifiers and
fixtures are tracked under `validation/`; 52 compiled regression controls pass.
The archived inputs include that directory. `audio-data-validation-20261101.json`
links the integrated reports and module metadata. All source changes are in
recipe patches 0121 through 0123; no build-warning or KMI bypass was introduced.

The old temporary handoffs are frozen and integrated. Do not run their setup or
integration scripts again. Future Bazel or staging operations share
`flock /tmp/onyx-6.18-build.lock`. Use the tracked build/stage scripts from this
port directory, not the top-level 6.6 scripts. Current source/header hashes are
in the per-group validation records. Keep baseline/common source pins intact.

Next work: the WCD93xx codec/slave variants, WSA/third-party amplifiers and
machine/audio consumers; remaining data, charging, diagnostic and peripheral
drivers; matched vendor-ramdisk/system/vendor DLKM and boot images; then phone
bring-up and KernelSU/NetHunter integration. `module-coverage-20261031.json`
contains the exact remaining-name inventory. Establish actual provider and DT
requirements when deciding whether an alternative module is needed.

SHS OOM reclaim intentionally handles only the current online CPU. Stock GKI
does not retain `cpus_read_trylock`, and blocking CPU-hotplug locks from OOM
could deadlock. The normal RMNET paths remain native. Read the port records for
the clock, codec, SoundWire and provider lifetime contracts and test limits.

Onyx WLAN is ICNSS2/HIF_IPCI. ROM-listed CNSS2/PCIe/MHI support is not evidence
that this phone needs a standard PCIe/MHI host for its active Wi-Fi transport.
The selected custom monitor/direct-injection WLAN source pin is preserved.

User status estimate given: roughly 2–4 more working days to a first testable
image, with low confidence. Overall completion cannot be timed reliably before
boot and hardware testing. Module-name coverage is not hardware functionality.
