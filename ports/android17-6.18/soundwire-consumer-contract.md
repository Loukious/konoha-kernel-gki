# SoundWire consumer and controller ownership

The 6.18 provider requires these contracts when porting native controllers and codecs.

- `get_matching_swr_slave_device()` returns a retained device. Every codec probe
  error and removal path must balance it with `put_device(&swr->dev)`.
- Controllers must retain their containing allocation through the final embedded
  master device release. A new `master.release` callback provides that hook;
  acquire a dedicated controller ownership reference before calling
  `swr_register_master`, and release it from this callback. The callback runs on
  registration failures as well as final unregister release. Do not devm-free
  the containing allocation while device references remain. Module callback
  lifetime needs review alongside the actual controller's ownership strategy.
- Native controller direct `gr_sid = 0` stores must use the framework's
  `state_lock` or `swr_set_device_group(..., SWR_GROUP_NONE)`. Codec writes to the
  cached SCP fields need conversion to atomic generation invalidation.
- Controllers must quiesce hardware/IRQ/asynchronous responders before final
  unregister. Provider unregister stops bus calls before removing children.
  Neither controller nor slave callbacks may unregister themselves synchronously:
  stop waits for active callbacks. Driver callbacks during probe retain the
  native readiness contract; physical probe/SSR interleavings are untested.
- Native port response IDs carry no generation. A controller must send exactly
  one response per successful request, and no late response after an errored
  request or after teardown. Actual native controllers respond synchronously;
  the provider cannot distinguish a stale duplicate ID after slot reuse.

