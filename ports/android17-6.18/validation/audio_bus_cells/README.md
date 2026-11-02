# Audio parent bus metadata

The proxy-disabled hardware run reaches audio child-device creation and panics
at `of_iommu_get_resv_regions+0x158`. The audio ION child has two native
`iommu-addresses` tuples, each encoded with one address and one size cell.
Its `/soc/spf_core_platform` parent lacks explicit bus-cell declarations.
The pinned 6.18 generic bus matcher rejects that parent, just as it rejected
the CVP parent before the earlier correction.

The recipe-owned overlay adds `#address-cells = <1>` and `#size-cells = <1>`
to the SPF parent. Every other node and property remains byte-identical,
including both audio reservation tuples and the earlier CVP correction.

`verify.py WORKSPACE ARTIFACT` compares both final board trees against the
previous archive. It compiles the pinned generic bus matcher and DMA region
helper using both audio tuples under ASan/UBSan. Missing address cells and
incorrect address/size widths are three compiled rejecting controls per board.
Identity DMA translation is mocked for these generic buses without DMA ranges.
It also audits every IOMMU reservation tuple in both final trees for a valid
phandle, explicit parent cell declarations, complete tuple and nonzero size.

All kernel/module bytes and the DTBO image must remain unchanged. A new module
inventory records the final board hashes while preserving compatible-node and
load-list evidence. Successful host checks do not prove hardware audio operation
or boot completion.
