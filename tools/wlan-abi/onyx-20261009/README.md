# Onyx WLAN ABI snapshot

Preserved from the existing local Konoha ABI build on October 18, 2026.
`Module.symvers` is the local ABI kernel export table; `vendor.symvers` is
its filtered vendor dependency table from the October 9 ROM build.
Together they match all 534 versioned imports in the archived installed
`qca_cld3_wcn7750.ko` (zero missing symbols or CRC mismatches).
The module release is `6.6.57-android15-8-4k`; the running kernel reports
6.6.143. Matching imports alone do not validate every structure ABI or make
hot replacement of the permanent WLAN module possible.

These files are explicit CI inputs on the investigation branch. They do
not replace the workflow defaults. No module CRC rewriting is performed.
