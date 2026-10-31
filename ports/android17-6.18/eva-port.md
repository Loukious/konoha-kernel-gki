# Native EVA accelerator port

One `msm-eva.ko` DDK target uses the recorded 24 ROM translation units and 53
fresh pinned vendor inputs. The native `CONFIG_EVA_SUN=1` and
`CONFIG_MSM_MMRM=1` defines, all eight exported kernel symbols, twenty DT aliases,
nine module dependencies, public ioctl ABI and FastRPC public header are
preserved. Dependencies use the real frozen FastRPC provider and the existing
Synx, MMRM, LLCC, mem-buf, SCM, firmware-loader and Gunyah providers.

The 6.18 adapters use current trace-string assignment and namespace syntax,
`strscpy`, explicit private/shared declarations and type-correct diagnostics.
Foreign-task descriptor access uses exported `fget_task_next`, requires the
returned descriptor to equal the requested descriptor, rejects FMODE_PATH,
and releases rejected file references. The native >=6.2 upstream-IOMMU-hint
placeholder was literal 1 and had no corresponding consumer in the pinned
IOMMU provider. Its EVA-only use is removed; LLCC and provider code are unchanged.

Ownership and input changes:

* Slab caches exist before driver registration and survive driver teardown.
  Root probe reserves one core, checks every fallible setup step, waits for
  child bindings, and publishes readiness after HFI/DSP setup. Partial failures
  unwind initialized resources and populated children. Dynamic cdev ownership
  prevents retained inode references from pinning freed core storage; opens
  check the actual cdev against the current ready core.
* Open instances and active fault callbacks retain the core. Root removal stops
  new opens and the DSP transport, then waits for sessions before removing HFI,
  children and core resources. Child removal only withdraws its own context bank
  and fault handler. Manual bind/unbind attributes are suppressed. Existing
  user sessions may finish while hardware is retained. A session or kernel
  client that never releases its ownership can delay removal indefinitely.
* HFI state, locks and workqueues precede IRQ registration. IRQs start disabled,
  and watchdog publication follows complete HFI setup. Failed second-IRQ setup
  releases the first IRQ; register-map errors propagate failure. Teardown
  disables IRQs, drains watchdog/power work, releases both IRQs and drains queued
  SSR work before freeing locks/storage. Managed MMIO mappings remain devres-owned.
* DSP startup handles error pointers and unregisters partially started transport.
  Exit withdraws callbacks, wakes and joins the command thread, cleans registered
  clients, and drains deferred FastRPC cleanup before destroying shared locks.
  Client removal first withdraws lookup visibility, then waits for existing
  references. Registration itself retains a node throughout probe/wait handling.
  Forced process-exit cleanup releases every session before the registry node.
  FastRPC callbacks queue cleanup so that unregister never waits for a callback
  blocked on the DSP command mutex; cleanup serializes with command processing.
* Debugfs instance reads hold the core list lock rather than dropping a final
  instance reference inside a protected read callback. Private debugfs data is
  freed after reader withdrawal; absent parent directories do not create orphan
  instance directories.
* DMA attachment accepts one complete contiguous mapping fitting the 32-bit
  firmware IOVA range. Empty, fragmented, short and overflowing mappings fail
  with attachment rollback. Physical address zero remains valid. Heap references,
  failed CPU mappings and attachment failures unwind correctly. Normal-memory
  vmap/vunmap pairs retain the actual address; unsupported I/O-memory mappings
  are unwound. Checksum reads are bounded by the buffer and paired with CPU
  access, and partial cache operations reject overflowing ranges.
* RPMsg checks exact payload sizes before any access, copies into aligned local
  storage and checks the distinct reply/command type ranges. Ioctl packet sizes
  have minimum, maximum and alignment checks; copies use bounded ranges and
  retain the validated size/type snapshot when userspace changes its header.

Validation uses seven actual-function ASan/UBSan harness groups: 3,745 RPMsg
bounds/alignment/type cases; 126 DMA/allocation/descriptor cases; 13 probe failure
cases and 40 core-drain races; nine HFI setup failures and 50 IRQ/workqueue
teardowns; two DSP startup failures and 50 worker/callback teardown sequences;
1,975 ioctl bounds/header-snapshot cases; 50 registry-reference races and 106
forced-session cleanups. Twenty compiled fault controls are rejected. The node,
IRQ and DSP harnesses mock subsystem services; the command-thread body, firmware
and entire instance-open/close state machine are not executed by those harnesses.
Build15 is clean and all 270 imported symbol CRCs match real providers. Generated
Kbuild, defines, module dependencies, exports, aliases, UAPI and exact source
replay are checked. Checkpatch reports zero errors, warnings and checks.

These are source/API/build and host-service checks. They do not establish
accelerator, secure assignment, Synx synchronization, native kernel VFS/IRQ/
IOMMU behavior, firmware negotiation, performance or phone operation. Native
firmware failure and fatal-watchdog policies remain, including potential
session/resource retention after a failed firmware teardown. The full HFI
protocol and remote-memory/VM paths have not been exhaustively audited or
exercised. This is not a boot or flash validation.

For integration, the root assigns the next patch number and merges the 53
imports, BUILD/Kconfig/config fragments and one module entry. Retarget both
`//onyx:` and `//onyx_fastrpc:` labels to local labels. Copy `verify_eva.py`,
`eva-sources.json` and all seven `tests/eva_*.c` together; the verifier accepts
`--package onyx` and `--source-root`. Copy frozen source through the root's
central staging workflow and run the normal full recipe CRC/archive validation.
No central recipe, onyx-dev, common, baseline or frozen provider source was edited.
