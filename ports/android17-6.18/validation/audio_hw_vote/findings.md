# LPASS TDM audio hardware vote — API contract regression

High-confidence cause of `msm_common_snd_hw_params lpass audio hw vote enable failed -22` and the resulting ASoC failures on `TDM-LPAIF_VA-RX-PRIMARY-VIRT-2`: port patch0079 added `|| !dev` to the digital codec resource manager's enable/disable guard, but the native and ported common-machine caller intentionally pass NULL as the diagnostic device.

Live evidence in bwmon-display-20261101T225345Z/live-618/logcat-all.txt:
- Lines58298–58306 at23:55:30.753,90120–90128 at23:58:01.981, and93051–93065 at23:58:07.512 show the hardware-vote EINVAL and its propagated TDM hw_params failure.
- Adjacent PAL metadata names PAL_STREAM_ULTRASOUND/ULTRASOUND_RX, and the Awinic startup path stops after the clock-vote error. This is a concrete failed audio stream, not merely noisy logging.

Exact path:
1. msm_common.c:455–462 enters the hardware-vote branch for QUIN MI2S/TDM; sound DT declares all seven interface hardware votes required.
2. mi2s_tdm_hw_vote_req at400 calls digital_cdc_rsc_mgr_hw_vote_enable(pdata->lpass_audio_hw_vote, NULL).
3. The ported manager's `IS_ERR_OR_NULL(vote_handle) || !dev` condition returns-EINVAL before `clk_prepare_enable`, even with a valid bound clock supplier.
4. The common machine propagates this failure before audio_prm_set_lpass_clk_cfg; ASoC then aborts backend hw_params.
5. Common disable and card reset also call digital_cdc_rsc_mgr_hw_vote_disable(handle,NULL); its matching guard would discard those releases even if another path acquired the vote.

Native baseline digital-cdc-rsc-mgr.c validates init/clock handle, accepts NULL `dev`, calls the actual clock API, and uses `dev` solely for diagnostic dev_dbg. Native msm_common.c already uses NULL on both vote calls. The port's per-clock ownership ledger likewise does not use `dev` for resource identity or lifetime. Rejecting NULL has no ownership justification and breaks the existing API contract.

Supplier audit: both `/soc/vote_lpass_core_hw` and `/soc/vote_lpass_audio_hw` are bound to the actual audio-ref-clk driver in live sysfs. The sound node binds sun-asoc-snd. Final DT clock provider source IDs are9(LPASS core) and11(LPASS audio), matching the actual provider enum; sound references the latter's phandle0x5a6 with clock index0. No clock provider needs to be stubbed, disabled or replaced. Early fw_devlink creation messages do not establish provider absence; subsequent probe success and live driver links show both suppliers bound. Current shell cannot read /proc/asound/cards, so sysfs/log evidence bounds readiness.

Correction: `/tmp/onyx-audio-hw-vote-proposed.patch` removes only `|| !dev` from enable and disable guards and documents the optional diagnostic-device parameter. It preserves invalid/error clock-handle validation, manager readiness, allocation/refcount checks, real clk_prepare_enable/clk_disable_unprepare, actual LPASS provider/PRM calls and SSR ownership ledger. No caller-specific workaround or error suppression is used.

Host validation: `/tmp/onyx-audio-hw-vote-validation/validation.json` and verify.py.
- Exact current common-machine mi2s_tdm_hw_vote_req, candidate digital manager body and exact provider callbacks execute together with lightweight CCF refcount/allocation/locking/PRM models. The real provider functions dispatch to the LPASS/DCODEC firmware request API, rather than a fake success callback.
- 280 lifecycle/accounting checks pass with ASan/UBSan: valid NULL-device acquisition/release, real-device callers, invalid/error handles, uninitialized/exited manager, nested common users1..128, allocation/CCF/firmware failures and successful retries, two hardware core types, unrelated CCF consumer preservation, partial SSR restoration, request overflow and final exit cleanup.
- Six compiled controls reject the previous implementation, independent enable-only and disable-only NULL rejection, bypassed real clock enabling, leaked failed first request, and lost manager readiness.
- Patch application dry-run passes against staged Onyx source. Strict checkpatch code checks pass with no errors/warnings (--no-signoff --ignore COMMIT_MESSAGE for this local raw diff; no authorship/signoff fabricated).

No recipe/staged-source or phone mutation, workspace build, flashing or reboot was performed. Candidate changes only digital-cdc-rsc-mgr.c, included in the SPF provider module; parent must rebuild/integrate and verify the TDM/ultrasound stream on hardware. Host tests cannot establish firmware behavior or live audio functionality.
