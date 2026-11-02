# Display bridge lifetime regression — 2026-11-01

High-confidence root cause for refcount warnings: vendor DSI bridge uses kzalloc and direct kfree, but Linux6.18 DRM bridges have reference-counted allocation ownership.

Runtime shell evidence: current camera-fp-20261101T222213Z/live-618/logcat-all.txt lines1114–1142 record addition on0/use-after-free warning at drm_bridge_attach -> dsi_drm_bridge_init. Lines1177–1203 record saturation/leak at drm_bridge_get. Lines1239–1265 record underflow/use-after-free at drm_bridge_put. These occur during display initialization. The warnings alone do not establish that this is the immediate source of camera preview lag.

Exact source chain:
- Vendor dsi_drm.c:1511 kzallocs struct dsi_bridge, leaving embedded base.refcount zero and base.containerNULL. At1521 drm_bridge_attach gets a reference. dsi_drm_bridge_cleanup at1535 directly kfree(bridge), bypassing held encoder references.
- common drm_bridge.c:226 drm_bridge_get calls kref_get; attach:423 acquires that ref unconditionally; detach:507 drops it. __drm_bridge_free:208 runs bridge destroy then kfree(bridge->container).
- Modern __devm_drm_bridge_alloc:264 allocates the container, sets funcs/container, initializes kref1 and adds device release action. devm_drm_bridge_alloc in drm_bridge.h:1187 wraps it. Attach succeeds with refs2; encoder detach drops to1; owner devres release drops to0 and frees exactly once.
- Master component lifecycle provides the appropriate owner: component.c try_to_bring_up_aggregate_device:245 opens devres group on adev->parent across msm_drm_bind, and on bind failure releases group. take_down_aggregate_device:280 calls master unbind then releases that group.
- msm_drv.c:2359 msm_drm_bind calls msm_drm_component_init; msm_drm_unbind calls msm_drm_uninit. msm_drm_uninit:534 calls drm_mode_config_cleanup before aggregate group release. drm_encoder.c:197–199 encoder cleanup iterates attached bridge chain and detaches each bridge.
- Allocate with dev->dev (DRM master), because allocations happen in master KMS hw_init after component_bind_all. The display component devres group is already closed at that point, so allocating on display->pdev does not get the same failure/rebind lifetime. Master group is still open and correctly owns all these bridges.

Proposed module-only correction: /tmp/onyx-display-bridge-proposed.patch changes DSI bridge allocation to devm_drm_bridge_alloc(dev->dev, struct dsi_bridge, base, &dsi_bridge_ops), handles ERR_PTR allocation failures, removes manual attach-failure free and removes cleanup/free helper. Display deinit clears its association; encoder cleanup and master devres group own release. This also handles the failure paths that clear display association before sde_encoder_destroy; the bridge remains alive through encoder cleanup.

Do not use devm_drm_put_bridge here: core drm_bridge.c:1429 explicitly says that temporary workaround MUST NOT be used. Do not retain kfree after managed allocation; it causes double free/stale encoder references. Do not manually initialize kref without setting valid container/lifetime.

Import availability: archived GKI kernel_aarch64_Module.symvers exports __devm_drm_bridge_alloc with CRC0x35521c16. No GKI byte change is required; normal module import checks should validate it.

Suggested exact-source validation: allocation failure, devm-action failure, bridge attach failure and all refs released through outer group; successful attach2 refs; association clear before encoder cleanup preserves object; encoder detach then group release frees once; group owner must be passed dev->dev. Candidate must reject the original uninitialized allocator and a retained early kfree regression. Repeated bind failure/rebind group cleanup must leave no live bridge allocations.

No shared source/recipe/build mutation was performed, and no firmware or native disassembler analysis was needed. This is ordinary-source and recorded runtime evidence. Hardware result after correction remains pending.
