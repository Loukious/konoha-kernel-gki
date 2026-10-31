#!/usr/bin/env python3
"""Compare every node and property with the captured ROM's merged device trees."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from build_devicetrees import digest, read_fdt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path, help="device-tree build artifact directory")
    parser.add_argument("baseline", type=Path, help="baseline DTB_OBJ/out directory")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    artifact, baseline = args.artifact.resolve(), args.baseline.resolve()
    config = json.loads((artifact / "inputs/devicetrees.json").read_text())
    report = {"baseline_sha256": {}, "artifact_sha256": {}, "trees": {}}
    for name in [*config["dtb_order"], config["board_overlay"]]:
        report["baseline_sha256"][name] = digest(baseline / name)
        report["artifact_sha256"][name] = digest(artifact / "merged" / name)
    with tempfile.TemporaryDirectory(prefix="onyx-dt-parity-") as temporary:
        for name in config["dtb_order"]:
            target = Path(temporary) / name
            subprocess.run([str(artifact / "tools/fdtoverlay"), "-i", str(baseline / name),
                            "-o", str(target), str(baseline / config["board_overlay"])], check=True)
            old, new = read_fdt(target), read_fdt(artifact / "checked" / name)
            if old != new:
                raise ValueError(f"device tree differs from ROM baseline: {name}")
            report["trees"][name] = {"identical_nodes_and_properties": True, "nodes": len(new),
                                    "properties": sum(len(p) for p in new.values())}
    report["limits"] = "Device-tree parity only; does not establish 6.18 driver compatibility or boot."
    result = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
