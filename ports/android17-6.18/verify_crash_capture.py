#!/usr/bin/env python3
"""Exercise actual memory-dump and DCC helpers with sanitized host shims."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from verify_minidump import function

HERE = Path(__file__).resolve().parent


def structure(source, name):
    match = re.search(r"^struct " + name + r" \{.*?^\};", source, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_tree", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.staged_tree.resolve()
    paths = [root / "drivers/soc/qcom/memory_dump_v2.c",
             root / "drivers/soc/qcom/dcc_v2.c",
             root / "include/soc/qcom/memory_dump.h"]
    mem, dcc, header = [re.sub(r"/\*.*?\*/", "", p.read_text(), flags=re.S) for p in paths]
    constants = "\n".join(line for line in mem.splitlines()
                          if line.startswith("#define ") and not line.endswith("\\"))
    constants += "\n" + "\n".join(line for line in header.splitlines()
                                    if line.startswith("#define "))
    names = ["sprs_dump_data", "cpuss_regdump_data", "cpuss_dump_data",
             "reg_dump_data", "msm_dump_table", "msm_memory_dump"]
    structs = structure(header, "msm_dump_entry") + "\n"
    structs += "\n".join(structure(mem, name) for name in names)
    mem_functions = ["init_memory_dump", "msm_dump_get_table", "register_dump_table_entry",
                     "__update_reg_dump_table", "update_reg_dump_table"]
    code = "\n".join(function(mem, name) for name in mem_functions)
    code += "\n" + function(dcc, "dcc_sram_memcpy") + "\n" + function(dcc, "dcc_sram_read")
    unit = (HERE / "tests/crash_capture_harness.c").read_text().replace(
        "/* DRIVER_TYPES */", constants + "\n" + structs).replace("/* DRIVER_CODE */", code)
    with tempfile.TemporaryDirectory(prefix="onyx-crash-capture-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(unit)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-sign-compare", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer", "-pthread", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
    report = {
        "result": "passed",
        "checks": ["336-entry full table, overflow rejection, concurrent registration",
                   "Invalid root counts, table IDs, types and physical offsets",
                   "CPU register-table count overflow and rejected-write preservation",
                   "All byte ranges around 64-byte DCC SRAM, allocation/user-copy failure",
                   "DCC reads after device removal and serialized in-flight reads",
                   "AddressSanitizer and UndefinedBehaviorSanitizer"],
        "source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in paths},
        "limits": "Host helpers only; no firmware handoff, physical allocation, sysfs, PM or phone test."
    }
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
