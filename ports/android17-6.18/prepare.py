#!/usr/bin/env python3
"""Create an isolated, revision-pinned Android 17/6.18 build workspace."""

import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


HERE = Path(__file__).resolve().parent


def run(command, cwd):
    subprocess.run(command, cwd=cwd, check=True)


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--init-only", action="store_true")
    mode.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    if (workspace / ".git").exists():
        parser.error("workspace must not be an existing source repository")
    pins = json.loads((HERE / "sources.json").read_text())
    manifests = {
        "onyx-port.xml": (HERE / "onyx-port.xml").read_bytes(),
        "dependency-lock.xml": (HERE / "dependency-lock.xml").read_bytes(),
    }
    expected = {"common": pins["common"]["revision"]}
    expected.update({p.get("path"): p.get("revision") for p in
                     ET.fromstring(manifests["dependency-lock.xml"])})
    expected.update({p["workspace_path"]: p["revision"] for p in pins["baseline"]})
    local_manifests = workspace / ".repo/local_manifests"
    if (workspace / ".repo").is_dir():
        source = workspace / ".repo/manifests"
        if (git(source, "remote", "get-url", "origin") != pins["manifest"]["url"]
                or git(source, "rev-parse", "HEAD") != pins["manifest"]["revision"]):
            parser.error("existing workspace has a different manifest; use a new directory")
        for path in local_manifests.glob("*.xml"):
            if path.name not in manifests or path.read_bytes() != manifests[path.name]:
                parser.error(f"preserve and review unexpected local manifest: {path}")
    else:
        if args.verify_only:
            parser.error("no prepared workspace at this path")
        if workspace.exists() and any(workspace.iterdir()):
            parser.error("workspace must be empty")
        workspace.mkdir(parents=True, exist_ok=True)
        command = ["repo", "init", "-u", pins["manifest"]["url"], "-b",
                   pins["manifest"]["revision"], "--depth=1", "--no-clone-bundle"]
        if args.reference:
            command.append(f"--reference={args.reference.resolve()}")
        run(command, workspace)
    if not args.verify_only:
        local_manifests.mkdir(exist_ok=True)
        for name, contents in manifests.items():
            (local_manifests / name).write_bytes(contents)
        (workspace / "sync-projects.json").write_text(json.dumps(list(expected), indent=2) + "\n")
    if args.init_only:
        print(f"Initialized {workspace}; {len(expected)} projects locked")
        return
    pending = []
    for path, revision in expected.items():
        root = workspace / path
        if not (root / ".git").exists():
            pending.append(path)
        elif git(root, "status", "--porcelain", "-uno"):
            parser.error(f"tracked changes in {path}; preserve them before preparing sources")
        elif git(root, "rev-parse", "HEAD") != revision:
            pending.append(path)
    if pending and args.verify_only:
        parser.error(f"projects do not match the lock: {', '.join(pending)}")
    if pending:
        run(["repo", "sync", "-c", "--no-manifest-update", "--no-tags", "--no-clone-bundle",
             f"-j{args.jobs}", *pending], workspace)
    for path, revision in expected.items():
        if git(workspace / path, "rev-parse", "HEAD") != revision:
            parser.error(f"revision mismatch after sync: {path}")
    print(f"Verified {len(expected)} pinned projects in {workspace}")


if __name__ == "__main__":
    main()
