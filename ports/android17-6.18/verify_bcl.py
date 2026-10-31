#!/usr/bin/env python3
"""Exercise staged PMIC BCL arithmetic, snapshot failures and IRQ state helpers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
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
    source = args.staged_tree / "drivers/thermal/qcom/qti_bcl_pmic5.c"
    header = args.staged_tree / "drivers/thermal/qcom/qti_bcl_common.h"
    ctext, htext = source.read_text(), header.read_text()
    constants = "\n".join(re.findall(
        r"^#define (?:BCL_(?:IBAT|VBAT|TRIGGER)\w*|BPM_\w+|EXTEND_BIT) [^\n]+",
        ctext, re.M))
    definitions = structure(htext, "bcl_bpm_stats")
    names = ["convert_adc_nu_to_mu_val", "convert_ibat_to_adc_val",
             "convert_adc_to_ibat_val", "convert_ibat_to_ccm_val",
             "get_bpm_stats", "bcl_set_lbat_irq", "bcl_irq_quiesce"]
    helpers = "\n".join(function(ctext, name) for name in names)
    unit = (HERE / "tests/bcl_harness.c").read_text().replace(
        "/* DRIVER_CONSTANTS */", constants).replace(
        "/* DRIVER_TYPES */", definitions).replace("/* DRIVER_HELPERS */", helpers)
    with tempfile.TemporaryDirectory(prefix="onyx-bcl-test-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(unit)
        subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
    report = {"result": "passed", "helpers_exercised": names,
              "checks": ["All 65536 signed ADC values at three nano-unit scales",
                         "Legacy ADC and threshold multiplication at integer boundaries",
                         "CCM thresholds saturate before narrowing",
                         "BPM little-endian snapshot and injected HOLD/read/release failures",
                         "Failed snapshots preserve output and release successful HOLD",
                         "IRQ rearm, wake failure and teardown with late callbacks",
                         "AddressSanitizer and UndefinedBehaviorSanitizer"],
              "source_sha256": {str(p.relative_to(args.staged_tree)):
                                hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [source, header]},
              "limits": "Host helper checks only; no real IRQ concurrency, regmap, firmware or phone test."}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
