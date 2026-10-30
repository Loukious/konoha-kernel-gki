#!/usr/bin/env python3
"""Record the Onyx source/build inputs and map its module load stages."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def module_name(value):
    return Path(value).name.removesuffix(".ko").replace("-", "_")


def load_list(path):
    result = []
    for line in path.read_text().splitlines():
        line = line.partition("#")[0].strip()
        if line and line not in result:
            result.append(line)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runtime-modules", type=Path)
    parser.add_argument("--extra-module", type=Path, action="append", default=[],
                        help="explicit existing .ko outside the standard ROM output directories")
    args = parser.parse_args()
    rom = args.rom_root.resolve()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output must be empty; preserve previous input snapshots")

    pins = json.loads(Path(__file__).with_name("sources.json").read_text())
    for source in pins["baseline"]:
        actual = git(rom / source["rom_path"], "rev-parse", "HEAD").decode().strip()
        if actual != source["revision"]:
            parser.error(f"{source['rom_path']}: expected {source['revision']}, got {actual}")

    kernel = rom / "kernel/xiaomi/sm8735"
    device = rom / "device/xiaomi/onyx"
    objects = rom / "out/target/product/onyx/obj"
    core_out = objects / "KERNEL_OBJ"
    required_files = {
        "config": core_out / ".config",
        "symvers": core_out / "Module.symvers",
        "kernel.release": core_out / "include/config/kernel.release",
        "modules.builtin": core_out / "modules.builtin",
        "platform-first-stage.list": kernel / "modules.list.msm.sun",
        "device-first-stage.list": device / "modules/modules.list.first_stage",
        "second-stage.list": device / "modules/modules.list.second_stage",
        "vendor-dlkm.list": device / "modules/modules.list.vendor_dlkm",
        "vendor-blocklist": kernel / "modules.vendor_blocklist.msm.sun",
        "BoardConfig.mk": device / "BoardConfig.mk",
    }
    for path in required_files.values():
        if not path.is_file():
            parser.error(f"required baseline input is missing: {path}")
    for path in args.extra_module:
        if not path.is_file() or path.suffix != ".ko":
            parser.error(f"extra module must be an existing .ko: {path}")

    output.mkdir(parents=True, exist_ok=True)
    snapshots = output / "snapshots"
    snapshots.mkdir()
    sources = []
    for source in pins["baseline"]:
        path = rom / source["rom_path"]
        # Capture changes without modifying or cleaning the user's ROM checkout.
        patch = git(path, "diff", "--binary", "HEAD")
        patch_name = source["rom_path"].replace("/", "_") + ".patch"
        (snapshots / patch_name).write_bytes(patch)
        status = git(path, "status", "--porcelain", "-uno").decode().splitlines()
        sources.append({
            **source,
            "tracked_changes": len(status),
            "tracked_patch": f"snapshots/{patch_name}",
            "tracked_patch_sha256": hashlib.sha256(patch).hexdigest(),
        })

    inputs = {}
    for name, path in required_files.items():
        destination = snapshots / name
        destination.write_bytes(path.read_bytes())
        inputs[name] = {"source": str(path), "sha256": digest(destination)}

    builds = {}
    build_counts = {}
    for label, path in [("core", core_out), ("external", objects / "sm8735-modules")]:
        count = 0
        for module in sorted(path.rglob("*.ko")):
            key = module_name(module.name)
            relative = module.relative_to(path).as_posix()
            builds.setdefault(key, []).append({
                "tree": label,
                "object_path": relative,
                "sha256": digest(module),
            })
            count += 1
        build_counts[label] = count
    for module in args.extra_module:
        builds.setdefault(module_name(module.name), []).append({
            "tree": "explicit_extra",
            "object_path": str(module.resolve()),
            "sha256": digest(module),
        })
    build_counts["explicit_extra"] = len(args.extra_module)

    builtin = {module_name(name) for name in load_list(required_files["modules.builtin"])}
    blocklist = set()
    for line in load_list(required_files["vendor-blocklist"]):
        parts = line.split()
        if len(parts) == 2 and parts[0] == "blocklist":
            blocklist.add(module_name(parts[1]))
    stages = {
        "first_stage": load_list(required_files["platform-first-stage.list"])
            + load_list(required_files["device-first-stage.list"]),
        "second_stage": load_list(required_files["second-stage.list"]),
        "vendor_dlkm": load_list(required_files["vendor-dlkm.list"]),
    }
    inventory = {}
    missing = {}
    for stage, names in stages.items():
        rows = []
        seen = set()
        for name in names:
            key = module_name(name)
            if key in seen:
                continue
            seen.add(key)
            row = {"module": name, "name": key, "blocked": key in blocklist,
                   "builtin": key in builtin, "build_outputs": builds.get(key, [])}
            rows.append(row)
        inventory[stage] = rows
        missing[stage] = [r["module"] for r in rows
                          if not r["build_outputs"] and not r["builtin"] and not r["blocked"]]

    runtime = None
    if args.runtime_modules:
        rows = [line.split() for line in args.runtime_modules.read_text().splitlines()]
        names = {module_name(row[0]) for row in rows}
        runtime = {
            "source_sha256": digest(args.runtime_modules),
            "loaded": len(names),
            "not_in_baseline_build": sorted(names - builds.keys() - builtin),
        }

    config = required_files["config"].read_text()
    compiler = re.search(r'^CONFIG_CC_VERSION_TEXT="(.*)"$', config, re.M)
    result = {
        "sources": sources,
        "inputs": inputs,
        "compiler": compiler.group(1) if compiler else None,
        "kernel_release": required_files["kernel.release"].read_text().strip(),
        "build_counts": build_counts,
        "module_stages": inventory,
        "missing_required_build_outputs": missing,
        "runtime_comparison": runtime,
        "limits": [
            "Existing ROM build outputs are inventoried, not rebuilt or certified against current source.",
            "Tracked patches are captured; generated and untracked files are not copied.",
            "The separate pinned onyx-wlan repository is the WLAN source of record; do not reconstruct it solely from the modules-tree patch.",
            "Filename mapping is not an ABI or runtime compatibility check.",
        ],
    }
    (output / "inventory.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({
        "output": str(output),
        "build_counts": build_counts,
        "stage_counts": {key: len(value) for key, value in inventory.items()},
        "missing": missing,
        "runtime": runtime,
    }, indent=2))


if __name__ == "__main__":
    main()
