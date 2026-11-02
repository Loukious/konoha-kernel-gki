# GPI ring pointer validation

Patch 0141 reconstructs event-ring RP from the hardware register's low 32
DMA-address bits. It subtracts the low ring base in unsigned 32-bit arithmetic,
then checks the resulting offset against the allocation and element alignment.
The existing allocator guarantees a power-of-two aligned ring of at most 32 KiB,
so the ring cannot cross a 4 GiB boundary. Full transfer-completion pointers
retain their upper bits; Q2SPI completion pointers combine the low word and
the high byte provided by the event.

`verify.py WORKSPACE` extracts six functions directly from the staged driver,
compiles them with the workspace's pinned Clang and ASan/UBSan, and exercises
14,807 cases. These cover low and high IOVAs, every aligned offset in three
ring sizes, wraparound, malformed pointers, empty/zero-size rings, all start/end
combinations for draining, a moving producer, error callbacks, acknowledgements,
doorbells and channel-specific stale marking. Five compiled regressions must
fail. The old conversion's unreachable target is reproduced with integer
offsets, without constructing an out-of-bounds C pointer.

The failed duplicate-funnel hardware test recovered a CPU0 hard-lockup panic
and 11,403 SPI callback lines. `cb_ev=5` identifies
`MSM_GPI_QUP_EOT_DESC_MISMATCH`; `status=2435` is the driver's source line for
an event without a pending descriptor. It is not a hardware status register.
The CPU7 panic stack belongs to the watchdog detector and does not locate the
blocked CPU0 instruction. The callback storm overwrote early ring addresses.
These observations are consistent with the pointer bug but do not establish
that a high IOVA caused the failed boot.

The subsequent repaired hardware test (`gpi-rings-20261101T193410Z`) captured
the event ring at DMA address `0x0ffffd9000`, with low hardware RP `0xfffd9010`
correctly reconstructed as `0x0ffffd9010`. It recorded no SPI callback storm,
and the NT36532 touchscreen probe and module initialization completed. Boot
then failed later, at 6.825 seconds, in OF IOMMU reservation parsing during
EVA initialization. Original 6.6 boot completion and all five rollback hashes
were verified. See `gpi-ring-validation-20261101.json` for the evidence paths.

Hardware test images set `msm_gpi.boot_trace=1` to record allocation DMA
addresses and the first valid event RP per ring allocation. Normal builds
leave this tracing off. The test preserves all existing module lists and
64-bit DMA support, and uses the same serial-loader diagnostic as the previous
test. The phone must be restored and partition hashes checked after a failed
test before preparing another candidate.
