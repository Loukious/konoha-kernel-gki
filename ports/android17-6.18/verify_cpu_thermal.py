#!/usr/bin/env python3
"""Validate actual CPU cooling workers with deterministic hotplug/WALT adapters."""

import argparse
import hashlib
import json
from pathlib import Path
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
    paths = [root / "drivers/thermal/qcom/thermal_pause.c",
             root / "drivers/thermal/qcom/cpu_hotplug.c",
             root / "kernel/sched/walt/walt_halt.c"]
    pause, hot, halt = [path.read_text() for path in paths]
    structs = pause[pause.index("struct thermal_pause_ctrl;"):pause.index("/* Protect the singleton")]
    structs += hot[hot.index("struct cpu_hot_ctrl;"):hot.index("/* Never hold this lock")]
    definitions = []
    for source, names in [
        (pause, ["thermal_pause_apply_cpu", "thermal_pause_target", "thermal_pause_update_work",
                 "thermal_pause_set_cur_state", "thermal_pause_get_cur_state", "thermal_pause_cleanup"]),
        (hot, ["cpu_hot_hp_online", "cpu_hot_execute_cdev", "cpu_hot_set_cur_state",
               "cpu_hot_get_cur_state", "cpu_hot_cleanup"]),
        (halt, ["walt_drain_pending", "walt_take_drain_requests"]),
    ]:
        definitions.extend(function(source, name) for name in names)
    template = HERE / "tests/cpu_thermal_harness.c"
    harness = template.read_text().replace("/* STAGED_STRUCTS */", structs)
    harness = harness.replace("/* STAGED_FUNCTIONS */", "\n".join(definitions))
    with tempfile.TemporaryDirectory(prefix="onyx-cpu-thermal-") as directory:
        path = Path(directory)
        (path / "harness.c").write_text(harness)
        subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-pthread",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-Werror=implicit-function-declaration", str(path / "harness.c"),
                        "-o", str(path / "harness")], check=True)
        run = subprocess.run([str(path / "harness")], check=True, text=True, capture_output=True)
    result = json.loads(run.stdout)
    result["sanitizers"] = ["AddressSanitizer", "UndefinedBehaviorSanitizer"]
    result["sha256"] = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in paths}
    result["harness_sha256"] = hashlib.sha256(template.read_bytes()).hexdigest()
    result["limits"] = "Host worker tests only; no kernel workqueue, CPU hotplug or thermal hardware execution."
    serialized = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end="")


if __name__ == "__main__":
    main()
