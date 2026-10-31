#!/usr/bin/env python3
"""Exercise actual staged minidump table helpers in a sanitized host harness.

This checks table/ELF arithmetic, not kernel locking or firmware operation.
Requires a host C compiler with AddressSanitizer and UndefinedBehaviorSanitizer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(source, name):
    match = re.search(r"^(?:static )?(?:inline )?[\w *]+\b" + name + r"\([^;]*?\)\n\{", source, re.M)
    if not match:
        raise ValueError(f"function not found: {name}")
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end]


def structure(source, name):
    match = re.search(r"^struct " + name + r" \{.*?^};", source, re.M | re.S)
    if not match:
        raise ValueError(f"structure not found: {name}")
    return match[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_tree", type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    paths = ["drivers/soc/qcom/msm_minidump.c", "drivers/soc/qcom/elf.h",
             "drivers/soc/qcom/minidump_private.h", "include/soc/qcom/minidump.h"]
    inputs = {p: (args.staged_tree / p).read_text() for p in paths}
    core, elf, private, public = [inputs[p] for p in paths]
    harness = (HERE / "tests/minidump_harness.c").read_text()
    definitions = [structure(public, "md_region"), structure(private, "md_ss_region"),
                   structure(private, "md_ss_toc"), structure(core, "md_table"),
                   structure(core, "md_elfhdr")]
    # Use the capacity constants from the driver rather than reproducing them.
    definitions += re.findall(r"^#define (?:MAX_NUM_ENTRIES|MAX_ELF_HEADERS|MAX_STRTBL_SIZE)[^\n]*", core, re.M)
    # md_table itself uses MAX_NUM_ENTRIES.
    definitions = definitions[-3:] + definitions[:-3]
    definitions += ["static struct md_table *minidump_table;",
                    "static struct md_elfhdr minidump_elfheader;",
                    "static unsigned int num_regions;",
                    "static int first_removed_entry = INT_MAX;",
                    "static bool is_rm_minidump, md_init_done;"]
    names = ["elf_sheader", "elf_section", "elf_pheader", "elf_program", "elf_str_table"]
    definitions += [function(elf, name) for name in names]
    names += ["elf_lookup_string", "set_section_name", "md_get_region", "md_region_num",
              "md_entry_num", "md_elf_entry_number", "md_add_elf_header",
              "md_update_elf_header", "msm_minidump_clear_headers", "md_add_ss_toc",
              "validate_region", "md_update_ss_toc", "md_remove_ss_toc",
              "msm_minidump_add_header"]
    definitions += [function(core, name) for name in names[5:]]
    unit = harness.replace("/* DRIVER_HELPERS */", "\n\n".join(definitions))
    for tag in ["elfhdr", "elf_shdr", "elf_phdr"]:
        unit = unit.replace("struct " + tag, tag)
    with tempfile.TemporaryDirectory(prefix="onyx-minidump-test-") as directory:
        path = Path(directory)
        (path / "test.c").write_text(unit)
        command = [args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", "-Wno-sign-compare", "-O1", "-g",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   str(path / "test.c"), "-o", str(path / "test")]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "test")], check=True)
    report = {"result": "passed", "helpers_exercised": names,
              "checks": ["SMEM and RM full-capacity ELF geometry",
                         "Distinct pending SMEM slots and ELF entries",
                         "Region lookup physical/virtual addresses",
                         "Remove every slot, compact names, and refill capacity",
                         "Rejected update/removal preserves table contents",
                         "AddressSanitizer and UndefinedBehaviorSanitizer"],
              "source_sha256": {p: hashlib.sha256(s.encode()).hexdigest() for p, s in inputs.items()},
              "limits": "Host table helpers only; no concurrency, firmware, panic, or device test."}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
