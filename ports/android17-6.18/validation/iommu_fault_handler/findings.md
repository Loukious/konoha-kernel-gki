# IOMMU fault-handler API and caller lifetime investigation

Read-only shared source/phone work. Proposed patch and verification artifacts only under /tmp; no recipe/staged mutation, reboot, or hardware fault injection.

## Finding and runtime evidence

Six iommu.c:2015 WARNs in bwmon-display-20261101T225345Z/live-618/logcat-all.txt: two at msm_smmu_probe [msm_drm], four at cvp_read_context_bank_resources_from_dt [msm_eva]. All six capture x8=1 at the cookie-type check, matching IOMMU_COOKIE_DMA_IOVA. Warning summary, domain pointers and exact log lines: /tmp/onyx-iommu-warning-summary.json. Display contexts are secure and nonsecure; EVA contexts are nonsecure, secure nonpixel, secure pixel and DSP. Nonsecure and DSP reuse the same domain pointer ffffff88160d1500 and the same core token.

6.6 baseline include/linux/iommu.h:95 stores iova_cookie separately from handler/handler_token. Baseline iommu.c:1974 setter writes handler/token without touching DMA cookie. Modern common/include/linux/iommu.h:222 puts IOVA cookie and handler/token in one tagged union; cookie enum:172. Modern iommu.c:2011 setter requires NONE and writes FAULT_HANDLER. Modern dma-iommu.c:369 allocates and tags DMA_IOVA; iommu.c:3079 installs that cookie when setting up default DMA domain. Display msm_smmu_probe and EVA msm_cvp_populate_context_bank retrieve existing default domains with iommu_get_domain_for_dev, then incorrectly call core setter. The warning returns without installing callback. Removing the check or writing the core handler directly would corrupt DMA allocation storage.

## Exact fault route and preserved behavior

Existing port already supplies correct separate storage: onyx/drivers/iommu/arm/arm-smmu/arm-smmu.h:512 fault_handler_lock/fault_handler/fault_token. qcom-iommu-util.c:675 resolves actual registered Qualcomm ops, rejects invalid/unsupported domains, and invokes arm_smmu_set_fault_handler at arm-smmu.c:2896. That setter holds fault_handler_lock; the threaded fault helper at arm-smmu.c:783 holds the same lock across callback dispatch. Clearing from outside callback waits for in-flight callbacks to finish and removes token and handler together.

arm_smmu_context_fault -> report_iommu_fault_helper reads FSR/FSYNR/FAR, builds flags and calls common report_iommu_fault. On default DMA domain the core reporter returns -ENOSYS, then existing SMMU private callback fallback runs. Its return value is preserved. No change is proposed to fault flags, dispatch helper, or core checks.

SMMU fault processing at arm-smmu.c:1026 retains -EBUSY to keep fault stalled without FSR clear/RESUME. -ENOSYS still triggers per-device callbacks, default register reporting, verification and existing nonfatal policy. Other return values follow native clear/terminate behavior. EVA callback updates fault state/debug records and returns -ENOSYS, retaining default fault evidence. Display callback currently returns -EINVAL despite its ENOSYS comment; patch preserves that existing native behavior. Without the handler registration fix, client diagnostics/recovery are absent and the fallback can reach BUG_ON(!non_fatal).

## Proposed correction and ownership

/tmp/onyx-iommu-proposed.patch changes only observed display/EVA source files.

Display registration uses qcom_iommu_set_fault_handler and returns errors. component_add failure clears/drains the handler, unlinks the client and clears drvdata before devres frees the client token. Successful remove calls component_del before synchronized callback clearing and list removal; no list lock is held across component_del.

Important ownership correction: msm_smmu_new -> msm_smmu_device_add -> msm_smmu_get_smmu (msm_smmu.c:399-458) only finds an existing OF-created context-bank client. Its MMU wrapper allocation owns no platform device. Original destroy unregisters that borrowed device. Pairing component_del in remove while retaining original destroy can recurse: component_master_del/component_del holds component_mutex -> msm_drm_unbind -> msm_drm_uninit -> kms destroy -> _sde_kms_mmu_destroy -> msm_gem_address_space_put -> MMU destroy -> platform_device_unregister -> msm_smmu_remove -> component_del tries the same mutex. References: common/drivers/base/component.c:278,820; msm_drv.c:536,2364; sde_kms.c:4883; msm_gem_vma.c:195; msm_smmu.c:248. Proposed destroy frees its wrapper only. Actual client removal owns component/handler/list cleanup, so aggregate MMU destruction cannot recursively unregister the child or double-delete its list entry. Handler remains valid during aggregate hardware teardown.

EVA setup checks private handler registration error and removes failed cb from the list; cb memory and dma_parms are devres owned. Child removal switches to synchronized private clearing. Because nonsecure and DSP share one core-owned handler, removal preserves it while another bank from a different child uses the same domain; the last child clears it. Parent cvp_release_core at cvp.c:287 marks ready=false, drains users (including actual callback references acquired by cvp_get_core:154), deinitializes HFI, depopulates children, then frees core. Child clearing thus runs before core token destruction. Callers must not clear inside their callback, as documented by the existing setter.

## Integration

No kernel-core change or new imports required. Apply source patch after current display/EVA lifetime patches. msm_drm already has qcom_iommu_util dependency. Add :qcom_iommu_util to msm_eva DDK deps (currently missing) for module symbol visibility and loading order; iommu_headers already supplies qcom-iommu-util.h. Build msm_drm + msm_eva and normal module/package checks.

## Verification and limits

- Patch dry-run against staged source passes.
- /tmp/onyx-iommu-verify.py extracts actual current report_iommu_fault, report_iommu_fault_helper, arm_smmu_set_fault_handler and qcom_iommu_set_fault_handler. ASan/UBSan checks DMA cookie preservation; domain/device/IOVA/flags/token; return0/-EINVAL/-ENOSYS/-EBUSY; core callback precedence; synchronized callback withdrawal. Result /tmp/onyx-iommu-dispatch-validation.json.
- /tmp/onyx-iommu-verify-callers.py extracts exact proposed display probe/remove/destroy, EVA setup/populate/remove/release-core and real CVP refcount functions. It exercises component_add error unwind, unsupported registration, successful component pairing, wrapper-only destruction, aggregate-unbind wrapper destruction, active callback drain before devres free, EVA buffer/domain/bus/allocation/registration failure cleanup, shared-domain child removal, and parent teardown waiting for active callback then depopulating children before core free. ASan/UBSan passes; /tmp/onyx-iommu-callers-validation.json records candidate source hashes. Optional positional source root reruns extraction against final staged tree.
- Host adapters model registers, devices/devres, DT, component services, mutexes and completion primitives. These tests execute actual extracted functions; they are not full DDK or in-kernel component teardown tests. Complete module compilation and live boot/repro remain required. No actual hardware fault was injected.
- Live acceptance: six API warnings absent, display/EVA probes bound, camera/preview/display/CVP behavior remains functional; existing real fault reporting remains enabled. If later hardware faults occur, verify client logs/returns still reach intended SMMU handling. Do not declare fault behavior tested solely from absence of boot warnings.

## Remaining consumer ledger

/tmp/onyx-iommu-ledger.json records applicability and followup separately: old WCN6750/6450 ICNSS DMA setter (dormant on current WCN7750); CNSS >=6.9 PCI fallback DMA setter and no-op unregister (compiled but not invoked by current ICNSS device); TVM smmu proxy DMA setter (not compiled; target uses common+pvm); valid unmanaged KGSL/GMU core callers unchanged. It also records two current EVA valued-VMID boolean API warnings and a latent missing-label NULL dereference separately; neither is silently folded into this handler patch.
