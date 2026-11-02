# Battery and Bluetooth live-boot corrections

The first completed Android boot under 6.18 rejected bq27z561 at probe with
`-EMSGSIZE`. Its manufacturer command returned less than the 32 bytes requested
by the port. Probe and vendor decoding need three bytes; charge parameter parsing
needs bytes 0..8. Device-name matching uses a five-byte prefix, now with a spare
NUL byte for its diagnostic string. The strict response bounds, checksum,
command echo, and transport-error checks remain in place.

Bluetooth failed to probe because ICNSS held GPIO 80, configured as
`wcn_sw_ctrl`. Both native board fragments requested the same mux; the native
Bluetooth driver ignored the error. A board compatibility overlay gives this
permanent mux to TLMM's default pinctrl state. Both radio state names remain,
with empty switch-control mappings. Their power, reset, clocks, other pin states,
and switch-control electrical configuration are unchanged. The overlay is
merged into the board overlay using the pinned overlay merge engine, then
checked on both boards with libfdt and Android libufdt.

`verify.py PREVIOUS_DT_DIRECTORY CANDIDATE_DT_DIRECTORY` compiles extracted
production MAC transactions with ASan/UBSan. It accepts valid variable-length
manufacturer responses and checks fixed field bounds, checksum/echo corruption,
transport errors, output bounds, and lock release. The prior probe fails this
suite. Final-tree checks prove exactly four ownership-related property changes,
two added nodes, unchanged named states and mux settings, and exactly one GPIO
80 mux owner on both board variants. Packaging re-runs these checks against the
full archive and confirms identical active compatible nodes and GKI bytes.

Hardware functionality requires the installation receipt and subsequent live
battery/Bluetooth observations; offline checks alone do not establish success.
