# SoundWire borrowed IRQ-domain ownership

The codec owns its WCD IRQ domain and handler data. The controller now looks up
private retained registrations instead of walking an unprotected slave list and
dereferencing a borrowed domain. Each registration has its own dispatch mutex;
unrelated slaves can continue dispatching during another slave's withdrawal.

`swr_set_slave_irq(slave, domain)` publishes a domain owned by the caller.
It fails before publication on invalid arguments,
master shutdown, allocation failure, or duplicate registration. It retains the
slave device without changing the public `swr_device` or `swr_master` layout.

`swr_set_slave_irq(slave, NULL)` excludes new reservations, waits for an active
handler, clears the borrowed pointer, and releases registration ownership.
Previously reserved dispatchers see NULL and drop their remaining references.
Thus a successful return permits freeing nested handlers and the domain even
if an inert reservation has not yet returned. Withdrawal works after controller
shutdown and succeeds if no registration exists. The owner must serialize its
lifecycle calls and hold no lock needed by a nested handler. Concurrent owner
withdrawal returns `-EBUSY`; an untracked direct pointer store returns `-EINVAL`.

Publishing or withdrawing from any nested slave IRQ callback is rejected with
`-EDEADLK` without changing registration; defer lifecycle operations until that
callback returns. Recursive nested dispatch is rejected. The private task
tracking is independent of lockdep. No registry mutex is held across callbacks.

`swr_handle_slave_irq(master, dev_num, dispatch)` reserves the controller and
registration, invokes the controller's mapping/dispatch callback once, and
returns the native pending flag. The controller keeps its pending/device-up
loop. Its callback never dispatches mapping zero. Keeping IRQ internals in the
controller preserves the bus provider's existing opaque types and export CRCs.
Device/module-driver
teardown must still quiesce its users before releasing owned state, as required
by the previous SoundWire and WCD-core provider contracts.

The separate patch modifies only `soc/soundwire.c`, `include/soc/soundwire.h`,
and the controller's nested dispatch block in `soc/swr-mstr-ctrl.c`. Existing
pins and three pre-patch hashes are recorded; this is a patch over the integrated
checkpoint, not a new import. `soc/regmap-swr.c` is an exact unchanged staged
companion because DDK omits cross-package C inputs from generated object lists.
Freeze verifies native `.mod`, `.cmd`, generated Kbuild/srcs.json, metadata,
every existing export CRC, imported CRCs, and zero-offset/fuzz patch replay.

Nine compiled host scenarios use the production registration/dispatch functions
and controller loop with device, IRQ, allocation and POSIX-thread service mocks.
Six compiled regressions are rejected. This is build and source-function
validation; physical IRQ delivery, Linux device removal and phone audio remain
untested. Root should integrate the provider patch before the WCD/WSA consumers,
map the temporary public header target back onto the central SoundWire header
target, and retain the existing central provider BUILD definitions.
