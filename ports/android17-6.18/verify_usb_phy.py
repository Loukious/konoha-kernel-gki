#!/usr/bin/env python3
"""Exercise staged USB PHY/repeater helpers with failing hardware-service models."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from verify_walt import function

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve() / "onyx"
    paths = [root / "include/linux/usb/onyx-power.h",
             root / "drivers/usb/repeater/repeater-qti-pmic-eusb2.c",
             root / "drivers/usb/phy/phy-msm-snps-eusb2.c",
             root / "drivers/usb/repeater/repeater.c"]
    power, repeater, phy, registry = [path.read_text() for path in paths]
    definitions = re.sub(r"^#include.*\n", "", power, flags=re.M)
    definitions += repeater[repeater.index("#define EUSB2_3P0"):repeater.index("/* Perform one")]
    definitions += "\n#define INIT_MAX_CNT 5\n"
    start = registry.index("struct usb_repeater_ref {")
    definitions += registry[start:registry.index("};", start) + 2] + "\n"
    for name in ["USB_PHY_APB_ACCESS_STATUS", "APB_READ_ACCESS_DONE",
                 "APB_ACCESS_POLL_DELAY", "APB_ACCESS_TIMEOUT"]:
        definitions += re.search(r"^#define\s+" + name + r"\s+.*$", phy, re.M)[0] + "\n"
    for source, names in [
        (repeater, ["eusb2_repeater_reg_read", "eusb2_repeater_bulk_reg_write",
                    "eusb2_repeater_reg_write", "eusb2_repeater_masked_write",
                    "eusb2_repeater_update_seq", "eusb2_repeater_get_version",
                    "eusb2_repeater_init", "eusb2_repeater_reset", "eusb2_repeater_validate_seq"]),
        (phy, ["msm_eusb2_phy_clocks", "msm_eusb2_validate_seq", "msm_eusb2_phy_set_power",
               "eusb2_phy_apb_cmd_wait"]),
        (registry, ["devm_usb_repeater_release_by_node"]),
    ]:
        definitions += "\n".join(function(source, name) for name in names)
    template = HERE / "tests/usb_phy_harness.c"
    harness = template.read_text().replace("/* STAGED_DEFINITIONS */", definitions)
    with tempfile.TemporaryDirectory(prefix="onyx-usb-phy-") as temporary:
        path = Path(temporary)
        (path / "harness.c").write_text(harness)
        subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-Werror=implicit-function-declaration",
                        str(path / "harness.c"), "-o", str(path / "harness")], check=True)
        run = subprocess.run([str(path / "harness")], check=True, text=True, capture_output=True)
    result = json.loads(run.stdout)
    result["sanitizers"] = ["AddressSanitizer", "UndefinedBehaviorSanitizer"]
    result["sha256"] = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in paths}
    result["harness_sha256"] = hashlib.sha256(template.read_bytes()).hexdigest()
    result["limits"] = "Host helper tests with simulated services; no USB enumeration, PMIC or PHY hardware execution."
    serialized = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end="")


if __name__ == "__main__":
    main()
