# ARM64 module exit relocation compatibility

Some vendor modules contain a valid `cleanup_module` function but relocate its
address to a different `struct module` offset than the running kernel expects.
Configuration-dependent fields can move `exit` while the cacheline-rounded
structure size remains unchanged. In that case, the existing size check passes,
`mod->exit` stays NULL, and `/proc/modules` reports `[permanent]`. The misplaced
relocation can also overwrite a different member of the kernel's descriptor.

This was observed while investigating release `nethunter-6.6.143-27`. The stock
WCN7750 module extracted from OS3.0.303.0.WOLMIXM has an `init_module` relocation
at `0x188` and a `cleanup_module` relocation at `0x5b8`. The local 6.6.143 kernel
reference build and the release's injection module use `exit` at `0x5a0`.
Both descriptors are 1536 bytes. The stock module SHA-256 is
`89d0985fdf05cc4888b726eb9579edd237abd81756ffe7c37ae795ceb5da2cdd`.
The bug report's configuration and the local reference build agree on all
configuration options in `struct module`. The installed phone's module has not
been independently hashed, so matching it to this stock image remains a device
verification step.

## General behavior

`fixup_module_exit_relocation()` in `kernel/module/main.c` derives the correct
destination using `offsetof(struct module, exit)`. It has no module-name,
firmware-version, or fixed-offset allowlist. Before layout and relocation
processing, it recognizes only descriptors with:

- The running kernel's descriptor size, with only the name initialized.
- Exactly two ARM64 absolute function-pointer relocations, without addends.
- A valid `init_module` relocation already at the kernel's `init` offset.
- A valid `cleanup_module` relocation to retained, allocated executable code.
- An aligned exit destination inside the descriptor, outside name and init.

For this recognized form, only the exit relocation's destination is changed.
Compatible descriptors are untouched. Missing cleanup functions, extra
relocations, initialized vendor fields, invalid symbols, and other ambiguous
layouts are left for the existing loader behavior to handle. The helper is
inactive when ARM64 or module unloading is disabled.

The change does not enable `CONFIG_MODULE_FORCE_UNLOAD`, bypass dependency or
reference-count checks, synthesize a cleanup function, or relax module signature
and version checks. This tree already tolerates symbol-version mismatches; this
fix does not make arbitrary incompatible modules safe. In particular, it does
not translate other structures or repair vendor code that directly accesses
incompatible `struct module` members.

The KSU packager also checks `rmmod`'s result. If removal fails, it restores the
previous Wi-Fi setting and stops the swap instead of attempting duplicate loads.

## Verification

The modified module loader compiles with the prepared ARM64 configuration,
both with module unloading enabled and with it disabled.
The host test compiles the actual production helper into a harness and reads
ELF files into private buffers; it never loads a module or changes an input file:

```sh
python3 tools/test_module_exit_relocation.py \
  /path/to/stock/qca_cld3_wcn7750.ko \
  /path/to/release-27/qca_cld3_wcn7750.ko \
  /path/to/extracted/stock/lib/modules
```

The stock corpus contained 394 modules: 347 recognized corrections and 47
unchanged descriptors. Including altered names, alternate source and target
offsets, compatible modules, and malformed inputs, 440 checks passed with
AddressSanitizer and UndefinedBehaviorSanitizer. Successful cases permit only
the exit relocation write; skipped cases must preserve every byte.

The test kernel was built from release 27 with Ubuntu Clang 18.1.3 and the same
KernelSU-Next revision. All 18,021 exported symbol names and CRCs matched release
27. The user subsequently reported the kernel and companion WLAN test package
working on device; a detailed per-operation test log was not supplied.

For other devices, check the relocation diagnostic and `/proc/modules`, then
test Wi-Fi shutdown, normal `rmmod`, replacement `insmod`, reconnect, and monitor
mode. This operates when a module is loaded, so it cannot repair modules already
resident in an older running kernel. No partition resize or `vendor_dlkm`
replacement is needed for this loader fix.
