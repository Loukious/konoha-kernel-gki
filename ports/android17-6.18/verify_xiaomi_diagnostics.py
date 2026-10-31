#!/usr/bin/env python3
"""Validate Xiaomi event framing/formatting and boot log bounds on the host."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from verify_minidump import function

HERE = Path(__file__).resolve().parent


def clean(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"^#include[^\n]*\n", "", text, flags=re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_tree", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.staged_tree.resolve()
    paths = [root / "drivers/misc/miev/midevice.c", root / "drivers/misc/miev/mievent.c",
             root / "include/miev/mievent.h", root / "drivers/xiaomi/boottime/boottime.c",
             root / "drivers/xiaomi/boottime/boottime.h", root.parent / "common/lib/kfifo.c"]
    device, event, event_header, boot, boot_header, fifo = [clean(p.read_text()) for p in paths]
    fifo_names = ["kfifo_unused", "kfifo_copy_in", "kfifo_copy_out", "kfifo_copy_to_user",
                  "__kfifo_peek_n", "__kfifo_poke_n", "__kfifo_len_r", "__kfifo_in_r",
                  "kfifo_out_copy_r", "__kfifo_out_r", "__kfifo_to_user_r"]
    code = "\n".join(function(fifo, name) for name in fifo_names)
    code += "\n" + "\n".join(function(device, name) for name in ["write_kbuf", "miev_read", "miev_write"])
    code += "\n" + event + "\n"
    code += "\n".join(function(boot, name) for name in ["bootprof_log_boot", "bootprof_free_logs"])
    unit = (HERE / "tests/xiaomi_diagnostics_harness.c").read_text().replace(
        "/* DRIVER_TYPES */", event_header + "\n" + boot_header).replace("/* DRIVER_CODE */", code)
    with tempfile.TemporaryDirectory(prefix="onyx-xiaomi-diagnostics-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(unit)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-sign-compare", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer", "-pthread", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        result = subprocess.run([str(path / "test")], check=True, capture_output=True, text=True)
        json_cases = 0
        for line in result.stdout.splitlines():
            if line.startswith("JSON "):
                payload = json.loads(line[5:])
                json_cases += 1
                assert payload == {chr(json_cases): chr(json_cases)}
            else:
                print(line)
        assert json_cases == 127
        print(f"Event JSON: {json_cases} keys/values decoded and matched")
    report = {
        "result": "passed",
        "checks": ["Actual 6.18 kfifo record implementation: wrap, eviction and copy errors",
                   "Short reads rejected without consumption; invalid writes preserve the queue",
                   "Independent event objects, integer extremes, 127 escaped JSON keys/values",
                   "4096-byte records accepted, oversized serialization rejected without consumption",
                   "Allocation failure injection and payload ownership",
                   "960-entry boot log capacity, block-allocation failure/retry and truncation",
                   "Concurrent boot log writers, AddressSanitizer and UndefinedBehaviorSanitizer"],
        "source_sha256": {str(p.relative_to(root.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in paths},
        "limits": "Host data paths only; no kernel module lifecycle, tracepoint, procfs or device test."
    }
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
