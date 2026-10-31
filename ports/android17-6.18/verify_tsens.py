#!/usr/bin/env python3
"""Check the staged TSENS temperature decoder with sanitized host register shims."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from verify_minidump import function, structure

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_tree", type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.staged_tree / "drivers/thermal/qcom/tsens.c"
    header = args.staged_tree / "drivers/thermal/qcom/tsens.h"
    ctext, htext = source.read_text(), header.read_text()
    definitions = "\n".join(structure(htext, name) for name in
                            ["tsens_features", "tsens_irq_data", "tsens_sensor"])
    names = ["code_to_degc", "tsens_version", "tsens_hw_to_mC"]
    functions = "\n".join(function(ctext, name) for name in names)
    unit = (HERE / "tests/tsens_harness.c").read_text().replace(
        "/* DRIVER_TYPES */", definitions).replace("/* DRIVER_HELPERS */", functions)
    with tempfile.TemporaryDirectory(prefix="onyx-tsens-test-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(unit)
        subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
    report = {"result": "passed", "helpers_exercised": names,
              "checks": ["All 4096 signed 12-bit v2 temperature values",
                         "Stable invalid-bit fallback and unstable-sample rejection",
                         "Read failures preserve caller output and propagate errno",
                         "Legacy ADC slope/offset conversion",
                         "AddressSanitizer and UndefinedBehaviorSanitizer"],
              "source_sha256": {str(p.relative_to(args.staged_tree)):
                                hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [source, header]},
              "limits": "Host decoding only; no register, interrupt, thermal policy or device test."}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
