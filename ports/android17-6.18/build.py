#!/usr/bin/env python3
"""Build the pinned 6.18 GKI and initial Onyx modules; archive and verify outputs."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from verify_modules import verify

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--jobs", type=int, default=12)
    parser.add_argument("--memory-mb", type=int, default=20000)
    args = parser.parse_args()
    if args.jobs < 1 or args.memory_mb < 1024:
        parser.error("jobs must be positive and memory-mb at least 1024")
    workspace = args.workspace.resolve()
    for script in ["prepare.py", "stage.py"]:
        command = [sys.executable, str(HERE / script), str(workspace)]
        if script == "prepare.py":
            command.append("--verify-only")
        subprocess.run(command, check=True)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    artifacts = workspace / "artifacts" / stamp
    artifacts.mkdir(parents=True)
    command = ["tools/bazel", "build", "--keep_going", f"--jobs={args.jobs}",
               f"--local_resources=memory={args.memory_mb}",
               "//common:kernel_aarch64", "//onyx:platform_modules"]
    print(f"Building; log: {artifacts / 'build.log'}", flush=True)
    with (artifacts / "build.log").open("w") as log:
        subprocess.run(command, cwd=workspace, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    execution_root = Path(subprocess.check_output(
        ["tools/bazel", "info", "execution_root"], cwd=workspace, text=True).strip())
    query = subprocess.check_output([
        "tools/bazel", "cquery", "--output=files",
        "//common:kernel_aarch64 + //onyx:platform_modules"
    ], cwd=workspace, text=True)
    (artifacts / "bazel-outputs.txt").write_text(query)
    outputs = [execution_root / line for line in query.splitlines()]
    kernel_dir = next(p.parent for p in outputs if p.name == "Image")
    release = (kernel_dir / "include/config/kernel.release").read_text().strip()
    modules = [p for p in outputs if p.suffix == ".ko"]
    catalog = json.loads((HERE / "modules.json").read_text())
    if {p.name for p in modules} != {entry["output"] for entry in catalog}:
        raise ValueError("unexpected platform module set")
    symvers = [p for p in outputs if p.name.endswith("_Module.symvers")]
    verification = verify(modules, symvers, release)

    for path in outputs:
        category = "platform" if "onyx" in path.parts else "gki"
        target = artifacts / category / path.name
        if path.is_file():
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(path, target)
    for name in ["modules_staging_dir.tar.gz", "out-dir-kernel-headers.tar.gz"]:
        shutil.copyfile(kernel_dir / name, artifacts / "gki" / name)
    for entry in catalog:
        target = entry["target"]
        config = kernel_dir.parent.parent / "onyx" / (target + "_config") / "out_dir/.config"
        shutil.copyfile(config, artifacts / "platform" / (target + ".config"))
    (artifacts / "gki/kernel.release").write_text(release + "\n")
    inputs = artifacts / "inputs"
    inputs.mkdir()
    for name in ["sources.json", "imports.json", "onyx-port.xml", "dependency-lock.xml",
                 "BUILD.bazel", "Kconfig", "scm_defconfig", "platform_defconfig", "modules.json",
                 "build.py", "prepare.py", "stage.py", "verify_modules.py", "boot-parameters.txt"]:
        shutil.copyfile(HERE / name, inputs / name)
    shutil.copytree(HERE / "patches", inputs / "patches")
    shutil.copyfile(workspace / "onyx/.staged-files.json", inputs / "staged-files.json")
    hashes = {}
    for path in sorted(artifacts.rglob("*")):
        if path.is_file():
            with path.open("rb") as stream:
                hashes[path.relative_to(artifacts).as_posix()] = hashlib.file_digest(
                    stream, "sha256").hexdigest()
    report = {
        "command": command,
        "kernel_release": release,
        "module_verification": verification,
        "sha256": hashes,
        "limits": [
            "Build and imported-symbol CRC checks only; no device boot or hardware validation.",
            "Not flashable: the rest of the Onyx driver stack and boot images are not ported.",
            "Gunyah secure-VM loading, proxy scheduling, memory assignment, and QRTR host transport are compiled together but have not been exercised with device firmware. Guest-power drivers are disabled, matching the Onyx HLOS baseline.",
            "The default Kleaf build uses the maybe-dirty suffix without stamping; input revisions are recorded separately.",
        ],
    }
    (artifacts / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Build and module CRC checks passed: {artifacts}")


if __name__ == "__main__":
    main()
