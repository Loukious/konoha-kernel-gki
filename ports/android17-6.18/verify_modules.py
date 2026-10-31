#!/usr/bin/env python3
"""Check the new modules' imported CRCs and vermagic against their build inputs.

This is a build-artifact check, not proof of hardware or runtime compatibility.
Requires kmod with extended-modversions support (tested with kmod 34.2).
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def verify(modules, symvers, release):
    exports = {}
    providers = {}
    for path in symvers:
        for line in path.read_text().splitlines():
            fields = line.split()
            if len(fields) < 3:
                continue
            crc, name = int(fields[0], 16), fields[1]
            if name in exports and exports[name] != crc:
                raise ValueError(f"conflicting provider CRCs for {name}")
            if name in providers and providers[name] != fields[2]:
                raise ValueError(f"duplicate symbol providers for {name}: "
                                 f"{providers[name]} and {fields[2]}")
            exports[name] = crc
            providers[name] = fields[2]

    result = []
    for module in modules:
        version = subprocess.check_output(
            ["modinfo", "-F", "vermagic", str(module)], text=True).strip()
        if not version.startswith(release + " ") or "modversions" not in version.split():
            raise ValueError(f"unexpected vermagic in {module}: {version}")
        imports = subprocess.check_output(
            ["modprobe", "--show-modversions", str(module)], text=True)
        checked = 0
        for line in imports.splitlines():
            crc, name = line.split()
            if exports.get(name) != int(crc, 16):
                raise ValueError(f"missing provider or CRC mismatch: {module.name}: {name}")
            checked += 1
        if not checked:
            raise ValueError(f"no versioned imports read from {module}; check kmod support")
        result.append({
            "module": module.name,
            "sha256": hashlib.sha256(module.read_bytes()).hexdigest(),
            "vermagic": version,
            "imports_checked": checked,
            "depends": subprocess.check_output(
                ["modinfo", "-F", "depends", str(module)], text=True).strip(),
            "aliases": subprocess.check_output(
                ["modinfo", "-F", "alias", str(module)], text=True).splitlines(),
        })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, action="append", required=True)
    parser.add_argument("--symvers", type=Path, action="append", required=True)
    parser.add_argument("--release", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(verify(args.module, args.symvers, args.release), indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
