#!/usr/bin/env python3
"""Exercise the staged WALT storage, pressure, counter and registration helpers."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(source, name):
    match = re.search(r"(?m)^(?:static (?:inline )?)?[\w *]+\b" + name
                      + r"\([^;]*?\)\n\{", source)
    if not match:
        raise ValueError(f"missing function {name}")
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve()
    files = {
        "task": root / "common/include/linux/sched/task.h",
        "public": root / "onyx/include/linux/sched/walt.h",
        "header": root / "onyx/kernel/sched/walt/walt.h",
        "core": root / "onyx/kernel/sched/walt/walt.c",
        "governor": root / "onyx/kernel/sched/walt/cpufreq_walt.c",
        "cpufreq": root / "onyx/kernel/sched/walt/walt_cpufreq_cycle_cntr_driver.c",
        "gclk": root / "onyx/kernel/sched/walt/walt_gclk_cycle_counter_driver.c",
        "sysctl": root / "onyx/kernel/sched/walt/sysctl.c",
    }
    sources = {key: path.read_text() for key, path in files.items()}
    definitions = []
    public = sources["public"]
    start = public.index("struct walt_task_struct {")
    end = public.index("\n};", start) + 3
    definitions.append(public[start:end])
    for name in ["android_task_vendor_data", "android_init_dynamic_vendor_data"]:
        definitions.append(function(sources["task"], name))
    for name in ["task_walt", "wts_to_ts"]:
        definitions.append(function(public, name))
    for name in ["walt_task_enqueue_runnable", "walt_task_dequeue_runnable",
                 "walt_capacity_orig_of"]:
        definitions.append(function(sources["header"], name))
    for name in ["walt_update_freq_pressure", "walt_actual_cpu_capacity",
                 "update_cpu_capacity_helper", "android_rvh_update_cpu_capacity"]:
        definitions.append(function(sources["core"], name))
    definitions.append(function(sources["governor"], "walt_uclamp_rq_util"))
    for key, end_name in [("cpufreq", "walt_cpufreq_cycle_cntr_driver_probe"),
                          ("gclk", "walt_gclk_cycle_counter_driver_probe")]:
        source = sources[key]
        start = source.index("#define CYCLE_CNTR_OFFSET") if key == "cpufreq" else source.index("struct gclk_counter {")
        end = source.index("static int " + end_name)
        definitions.append(source[start:end].replace("counter_ready", key + "_ready"))

    sysctl = sources["sysctl"]
    for name, body in re.findall(r"static const struct ctl_table (\w+)\[\] = \{(.*?)\n\};",
                                 sysctl, re.S):
        # A null sentinel is invalid for the new size-based registration API.
        if re.search(r"\{\s*\}", body):
            raise ValueError(f"null sysctl sentinel in {name}")
        count = len(re.findall(r"\.procname\s*=", body))
        if not count:
            raise ValueError(f"empty sysctl table {name}")
        definitions.append(f"static const struct ctl_table {name}[{count}] = {{0}};")
    definitions.append(sysctl[sysctl.index("struct walt_sysctl_registration {"):])

    template = HERE / "tests/walt_harness.c"
    harness = template.read_text().replace("/* STAGED_DEFINITIONS */", "\n".join(definitions))
    with tempfile.TemporaryDirectory(prefix="onyx-walt-test-") as directory:
        path = Path(directory)
        (path / "harness.c").write_text(harness)
        subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-Werror=implicit-function-declaration",
                        str(path / "harness.c"), "-o", str(path / "harness")], check=True)
        run = subprocess.run([str(path / "harness")], check=True, text=True, capture_output=True)
    result = json.loads(run.stdout)
    result["sanitizers"] = ["AddressSanitizer", "UndefinedBehaviorSanitizer"]
    result["sha256"] = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in files.values()}
    result["harness_sha256"] = hashlib.sha256(template.read_bytes()).hexdigest()
    result["limits"] = "Host helper tests only; no scheduler execution, hardware MMIO or phone boot."
    serialized = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end="")


if __name__ == "__main__":
    main()
