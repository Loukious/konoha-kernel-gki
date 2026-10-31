#!/usr/bin/env python3
"""Run the actual vendor ADC scaling implementation with sanitized host shims."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from verify_minidump import function

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_tree", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.staged_tree.resolve()
    source = root / "drivers/iio/adc/qcom-vadc-common.c"
    header = root / "include/linux/iio/adc/qcom-vadc-common.h"
    clients = root / "include/linux/adc-tm-clients.h"
    interpolation = root.parent / "common/include/linux/fixp-arith.h"
    client_text = clients.read_text().split("struct device;")[0]
    strip_includes = lambda text: re.sub(r"^#include[^\n]*\n", "", text, flags=re.M)
    unit = (HERE / "tests/adc_scaling_harness.c").read_text().replace(
        "/* DRIVER_TYPES */", strip_includes(client_text) + "\n#endif\n" +
        strip_includes(header.read_text())).replace(
        "/* DRIVER_CODE */", function(re.sub(r"/\*.*?\*/", "", interpolation.read_text(), flags=re.S),
                                     "fixp_linear_interpolate") +
        "\n" + strip_includes(source.read_text()))
    with tempfile.TemporaryDirectory(prefix="onyx-adc-scaling-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(unit)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-fmax-errors=5", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
    report = {"result": "passed",
              "checks": ["All 16-bit codes across hardware scale functions for Gen3/Gen4",
                         "Thermistor monotonicity including open-circuit end of ADC range",
                         "Forward/reverse temperature thresholds at every integer millidegree",
                         "Invalid scale, prescale and full-scale code rejection",
                         "AddressSanitizer and UndefinedBehaviorSanitizer"],
              "source_sha256": {str(p.relative_to(root.parent)):
                                hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [source, header, clients, interpolation]},
              "limits": "Host arithmetic only; no ADC register, IRQ, calibration or device test."}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
