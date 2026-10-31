#!/usr/bin/env python3
"""Materialize the Onyx module sources from pinned Git blobs and reviewed patches."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
RECEIPT = ".staged-files.json"


def hashes(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file() and p.name != RECEIPT}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    if not (workspace / ".repo").is_dir():
        parser.error("run prepare.py first")
    destination = workspace / "onyx"
    previous = None
    if destination.exists():
        receipt = destination / RECEIPT
        if (destination.is_symlink() or not receipt.is_file()
                or hashes(destination) != json.loads(receipt.read_text())):
            parser.error("onyx contains local changes; preserve them before restaging")
        previous = json.loads(receipt.read_text())

    with tempfile.TemporaryDirectory(prefix=".onyx-stage-", dir=workspace) as temporary:
        root = Path(temporary)
        for source in json.loads((HERE / "imports.json").read_text()):
            repo = workspace / source["project"]
            revision = subprocess.check_output(
                ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
            if revision != source["revision"]:
                parser.error(f"unexpected revision in {source['project']}; run prepare.py")
            blob = subprocess.check_output([
                "git", "-C", str(repo), "rev-parse", f"{revision}:{source['path']}"
            ], text=True).strip()
            if blob != source["blob"]:
                parser.error(f"unexpected source blob: {source['path']}")
            target = root / source["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(subprocess.check_output([
                "git", "-C", str(repo), "cat-file", "blob", blob
            ]))
        for patch in sorted((HERE / "patches").glob("*.patch")):
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                           cwd=root, check=True)
        for name in ["BUILD.bazel", "Kconfig", "scm_defconfig", "platform_defconfig"]:
            shutil.copyfile(HERE / name, root / name)
        contents = hashes(root)
        (root / RECEIPT).write_text(json.dumps(contents, indent=2) + "\n")
        if destination.exists():
            if previous is None or destination.is_symlink() or hashes(destination) != previous:
                parser.error("onyx changed while staging; preserve those edits before restaging")
            if contents == previous:
                print(f"{destination}: already up to date")
                return
            # Only replace files whose prior receipt we verified above.
            shutil.rmtree(destination)
        shutil.copytree(root, destination)
    print(f"Staged {len(contents)} files in {destination}")


if __name__ == "__main__":
    main()
