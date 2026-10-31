The main camera port retains the ROM's 148 compilation units and 485 exact
vendor inputs from modules revision `8500748dae32f10507c02f1c94bc971553cb525b`.
It reuses eleven already imported vendor headers and fifteen real provider
modules. Native SPECTRA feature defines, public UAPI inputs, device aliases,
module parameters and all 102 exported names are preserved. The artifact and
configuration reports record the exact built module and provider checksums.

The 6.18 adaptations cover platform removal callbacks, private helper
declarations, current allocation headers and DMA mapping APIs. Legacy ION
includes remain only in the native non-DMA-heap branch. Required DMA heaps
must exist; lookup references are released and unsupported I/O-memory vmaps
are unwound instead of being passed to normal-memory command parsers.

Camera file admission remains closed until all components and nodes are
initialized. Subdevice registration is transactional. Removal stops admission,
drains successful opens, unregisters video nodes and waits for final parent
V4L2 references before releasing child state. This also covers rejected opens
whose VFS release tail still accesses the subdevice. Native callbacks are
retained in each subdevice's copied operation table. Sync initializes before
publication and drains its event file, workqueue and video references.

DMA-fence and Synx registrations own their callback storage and backing
references. IRQ callbacks queue work rather than entering camera/provider
code under the fence lock. Withdrawal removes pending callbacks and waits for
running work before table slots can be reused. Synx retains its global
subscription and obtains the provider's current status. Sync quiesces producers
before clearing downstream objects and flushing its callback queue.

SMMU context setup uses the Qualcomm fault-handler API rather than replacing
the generic DMA domain cookie. Setup failures unwind only the current child.
Client and provider callbacks are withdrawn synchronously before their storage
is freed; context pools, DMA parameters and parent arrays have explicit owners.
Crash-dump registry entries use RCU publication and drained removal. Entries
check remaining capture capacity and copy bounded literal names. Request,
memory, SMMU, IFE, ICP and JPEG owners unregister before releasing their data.
JPEG and ISP unwind initialized contexts and managers without losing the
original error. JPEG releases its workqueues and all 64 contexts. I3C sensor
platform children belong to the camera master and are removed on probe failure
or removal. Event-injection writes join camera admission and teardown; the
diagnostic list is serialized and reset before core storage is freed.

Nine production-source ASan/UBSan suites pass and reject 92 compiled deliberate
regressions. They cover heap/vmap ownership, file and VFS-tail admission,
master/subdevice setup, DMA fences, Synx, sync lifecycle, SMMU, crash-dump
registration and JPEG/ISP setup. The individual reports state which functions
are extracted and which kernel services are mocked. Source replay must match
all 485 staged files with zero fuzz and zero offsets before integration.

Run the installed tests with:

```sh
python3 ports/android17-6.18/verify_main_camera.py /path/to/workspace \
    --source-dir onyx --negative --output /tmp/main-camera-validation.json
```

These checks do not establish camera HAL operation, sensor output, firmware,
real DMA/IOMMU/IRQ behavior, kernel VFS/RCU races or complete secure-VM operation.
The selected native configuration is HLOS; the native VMRM unregister-error
policy remains, and retained files or references can delay removal indefinitely.
Most sensor, ISP packet-processing and hardware recovery paths are preserved
native code rather than independently executed host tests. The I3C child and
event-injection ownership changes have source review and compilation evidence;
their complete kernel execution is not covered by the nine host suites.

Strict checkpatch reports zero errors, four warnings and retained style checks.
The warnings are the intentionally writable per-instance V4L2 operation table,
the native version-selection pattern around vmap unwind, and two vendor files
that already lack SPDX headers. The report remains unfiltered. Remaining checks
primarily concern native argument indentation, naming and integer typedefs on
functions whose visibility changed; they are not compiler diagnostics.
