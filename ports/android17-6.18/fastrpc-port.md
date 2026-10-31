# Native FastRPC provider for EVA

One native `frpc-adsprpc.ko` target, with the exact two ROM translation units and
six pinned vendor inputs. `CONFIG_DSP_SUN=1` and the RPMsg transport are retained.
The new `QCOM_FASTRPC_VENDOR` option is a DDK build marker for the vendor's
unconditional module; stock `QCOM_FASTRPC` and the trusted guest variant remain
disabled. All three public kernel-client exports, both DT aliases, native
module dependencies, public kernel header and userspace ABI header are preserved.

The 6.18 changes update trace-string assignment, namespace syntax, private
prototypes and platform removal. Permission inspection now uses the existing
mem-buf provider's copied ACL API and releases both allocations.

Ownership changes:

* Length checks precede all reply reads; aligned local copies handle unaligned
  transport payloads, invalid v2 versions and stale invocation contexts.
  User signal/notification lookup stays protected until the user access ends.
* Independent exported DMA buffers hold their channel reference and allocation
  context. Their release never consults the creating user, and mmap checks use
  the full allocation size. Teardown first drains invocations and cleans users,
  then unblocks session closes, then waits for outstanding exported DMA fds
  before removing context banks. This ordering permits session-before-buffer
  fd close. Holding such a DMA fd indefinitely also delays channel removal.
* Opens reject incomplete or withdrawn channels and undo failed IDA/QoS setup.
  Normal and post-SSR closes return only invoke counts they acquired; the
  FastRPC file operations now retain the module owner. Device references live
  until the final channel reference, and their last put occurs outside glock.
* Kernel-client DMA invocations use a count instead of a Boolean, and release
  only acquired mutexes. Private registry entries retain devices and callbacks,
  propagate probe failure, permit unregistering from one's own callback and
  wait for callbacks/probes in other tasks. The public structures stay unchanged.
* Context-bank setup commits only after fallible work succeeds, bounds both
  session and pooled-bank arrays, uses the DT element count, and removes only
  banks belonging to the actual device. Pooling/duplicate sessions retain their
  native behavior. Genpool creation preserves managed discovery; a devres group
  releases the pool before its DMA/IOMMU backing, handles negative/short map
  results and physical address zero, and frees temporary scatterlists.
* Partial RPMsg probe failures remove registered nodes, PDR handles, children,
  reserved-memory attachment and channel ownership. Module exit tears down the
  transport before unregistering the child driver. Debugfs is created before
  driver publication and session readers are withdrawn before user cleanup.

Validation: clean build7, 168 imported CRCs, native exports/aliases/dependencies,
exact generated two-unit Kbuild and DSP_SUN flags, unchanged public/UAPI headers,
zero-fuzz/zero-offset replay and clean checkpatch after excluding only the two
DOS_LINE_ENDINGS diagnostics for the native CRLF trace header. The raw log is
retained as checkpatch-raw.log; source line endings are preserved. Seven actual-function host
ASan/UBSan groups cover 2,680 wire bounds/alignment cases, registration failures,
self-unregister, 200 callback/probe races, 19 CB scenarios, 10 ACL/export cases,
23 transport scenarios, 100 transport/export-drain races, 13 kernel DMA error
cases, 100 concurrent DMA pairs and eight file-open/close cases. Twenty-one
compiled fault controls are rejected.

These are compile/API/host-service checks. No DSP firmware, secure ownership
transitions, kernel VFS/IRQ/devres behavior, genpool consumers or phone runtime
has been exercised. Native firmware failure policy is retained, including the
release-time fatal timeout and resource retention after failed secure memory
reclaim; these paths are not represented as recovered or hardware-validated.
The genpool consumer must return its allocations before its provider is removed,
as required by the native managed genpool contract. Manual child bind/unbind
attributes remain suppressed. The full DSP RPC marshalling implementation is
retained and was not exhaustively tested by this port's host harnesses.

Integration: assign the next patch number to `patch`, merge BUILD/Kconfig/config
fragments and six imports, copy frozen sources into the central development
staging tree through the root workflow, and add the one modules.json entry.
Retarget `//onyx:` dependencies to local labels when integrating the fragment.
Copy `verify_fastrpc.py`, `fastrpc-sources.json` and all eight `tests/fastrpc_*`
files together. The verifier accepts `--package onyx` for integrated checks.
Run the full recipe build and its normal CRC/archive checks before updating the
module coverage report. No central, pinned common/baseline, historical frozen
source or unrelated development source was edited by this handoff.
