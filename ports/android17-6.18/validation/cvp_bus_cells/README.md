# CVP parent bus metadata

The GPI-repaired hardware test reaches EVA initialization, then panics at
`of_iommu_get_resv_regions+0x158`. Its first CVP context bank references a
shared eight-tuple `iommu-addresses` property under the CVP parent. The native
parent omits `#address-cells` and `#size-cells`; both tuple fields inherit one
cell from `/soc` in Linux 6.6. Linux 6.18's `of_bus_default_match()` requires
an explicit `#address-cells` property, so DMA translation returns NULL and
the generic reservation parser later dereferences that NULL pointer.

The recipe-owned overlay declares one address and one size cell on the CVP
parent. It preserves native sources and adds no nodes or other properties.
`verify.py WORKSPACE ARTIFACT` compares every node and property on both final
boards, permits exactly these two additions, and verifies unchanged compatible
nodes, kernel/module bytes, DTBO image and all eight reservation tuples.
It also compiles the exact pinned `of_bus_default_match()` and
`of_translate_dma_region()` functions under ASan/UBSan. Identity DMA translation
is mocked for the actual generic-bus/no-DMA-ranges fixture. The native parent
must fail translation; all eight corrected reservations must preserve their
addresses and lengths. Missing metadata, two address cells and two size cells
are compiled rejecting controls.

The validator produces a new module inventory after confirming the active
compatible-node set is identical. The original inventory and earlier evidence
remain frozen. `verify_devicetrees.py` remains the strict native-parity checker;
it intentionally rejects these compatibility additions. This separate validator
documents their exact scope. Successful host translation does not prove phone
boot or accelerator operation; a hardware test is still required.
