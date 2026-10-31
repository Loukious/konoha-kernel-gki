# Native video codec port

The native `msm_video.ko` builds from 38 ROM compilation units and 89 pinned
inputs. Sun selection, both native Iris variants, platform tables, public UAPI,
four exported controls and all 36 aliases are retained. Its six native module
dependencies remain; `qcom_iommu_util` is added so context-bank fault callbacks
can be withdrawn and drained through the existing Qualcomm provider.

The 6.18 adaptations use the per-file V4L2 control handler, current VB2 queue
and device-removal signatures, namespaces, security and power-domain APIs.
File handles are installed before V4L2 core dispatch. Failed power resumes
return only acquired PM votes, and borrowed clock lookups are not released
as owned clock handles.

Core registration is serialized, rejects a second singleton and publishes
readiness only after media and interface setup. Both video nodes are withdrawn
before waiting for their final references. Core, media, queues and hardware
remain available while existing files close and retained instances drain.
Removal can wait indefinitely for a user that retains an open file or instance
reference. Instance destruction runs on an owned workqueue, preventing the
last debugfs reference from recursively waiting for its own callback. Explicit
debugfs storage ownership and final-put synchronization keep destructor state
alive through the drain.

Sessions publish after all lists, pools, work, file handles, VB2 queues, codec
controls and fences initialize. Later failures use the initialized unwind;
partial codec-control creation is also released. Media and component failures
balance OF references, child nodes and fence registration. Context-bank setup
restores borrowed DMA parameters or frees owned parameters, clears state for
rebind, and unregisters callbacks under the provider's dispatch mutex.

Unbind gates new opens, drains files and instances, withdraws debugfs producers,
cancels SSR and firmware work, deinitializes the core, synchronizes its IRQ and
drains power-collapse work before freeing queues and unbinding context banks.
Power-collapse requeue is gated during removal. Native retry on first session
open after an initial firmware startup failure remains intact.

Interface-queue initialization unwinds only current allocations and references,
resets borrowed views, and remains retryable. DMA heap references are released
after allocation. Failed CPU access, kernel mapping or attachment mapping
unwinds the associated buffers and references; allocation-size alignment uses
the full size_t width. Existing secure allocation and lending policy remains.

Validation: build12 passed without compiler/modpost warnings, all 269 imported
CRCs match their providers, generated Kbuild retains all 38 native units and
the native forced configuration header, strict checkpatch reports zero issues,
and replay from the pinned Git blobs matches all final bytes without fuzz or
offsets. Eight ASan/UBSan host suites pass 210 cases and reject 70 deliberate
regressions. They cover file/PM ownership (20), context banks (12), instance
references (4), media nodes (10), open initialization (29), teardown (7), queue
rollback/retry (96) and DMA allocation (32).

These tests extract production functions and use modeled kernel/provider
services. They do not establish real V4L2 scheduling, firmware compatibility,
DMA quiescence, IOMMU behavior, secure ownership transitions, codec output or
phone boot. Native hardware-failure handling is preserved; no successful reset
or recovery is claimed for persistent hardware errors.

Integration requires the frozen patch and 89 imports, BUILD fragments, one
module entry, eight verifier scripts and eight `tests/video_*` harnesses. Run
each verifier against the integrated `onyx` source and preserve the source,
provider, harness and common-header hash receipts. Original and frozen inputs
are immutable. The earlier lifetime-review.md records findings before their
implementation and is superseded by this record and the final receipts.
