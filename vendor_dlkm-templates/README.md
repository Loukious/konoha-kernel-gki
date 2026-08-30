# vendor_dlkm templates

`vendor_dlkm-evolution-20260828.img` — the vendor_dlkm image from the
Evolution X 16.0 onyx build (EvolutionX-16.0-20260828-onyx-11.10-Unofficial,
built 2026-08-28). 32,784,384 bytes.

This is the base the WLAN workflow repacks `qca_cld3_wcn7750.ko` into.
It MUST come from the ROM family the device actually runs: every module in
vendor_dlkm is loaded by that ROM's vendor HALs. A PixelOS-built image
flashed on an Evolution X install left the phone bootlooping (2026-08-30)
because all ~477 vendor modules were PixelOS builds — only the WLAN module
we build ourselves is safe to swap in.

Refresh this file whenever the Evolution X build changes its vendor modules:
copy `out/target/product/onyx/vendor_dlkm.img` over it, update the name's
date suffix, and update the sha256 in the workflows.
