#!/usr/bin/env python3
"""Compile and merge the pinned Onyx trees, then verify both bootloader variants."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
DT_PROJECT = "baseline/kernel/xiaomi/sm8735-devicetrees"
KERNEL_PROJECT = "baseline/kernel/xiaomi/sm8735"
MODULE_PROJECT = "baseline/kernel/xiaomi/sm8735-modules"
TOOL_PROJECT = "baseline/tools/dtc"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_fdt(path):
    """Read structure properties without relying on host Python libfdt versions."""
    data = path.read_bytes()
    if len(data) < 40:
        raise ValueError(f"short FDT: {path}")
    magic, total, structure, strings, _, version, _, _, strings_size, size = struct.unpack_from(
        ">10I", data)
    if (magic != 0xD00DFEED or total != len(data) or version < 17
            or structure + size > total or strings + strings_size > total):
        raise ValueError(f"invalid FDT header: {path}")
    nodes, stack = {}, []
    offset = structure
    end = structure + size
    while offset + 4 <= end:
        token, = struct.unpack_from(">I", data, offset)
        offset += 4
        if token == 1:
            stop = data.index(0, offset, end)
            name = data[offset:stop].decode()
            offset = (stop + 4) & ~3
            stack.append(name)
            node = "/" + "/".join(stack[1:])
            if node in nodes:
                raise ValueError(f"duplicate node {node}: {path}")
            nodes[node] = {}
        elif token == 2:
            if not stack:
                raise ValueError(f"unbalanced FDT: {path}")
            stack.pop()
        elif token == 3:
            length, nameoff = struct.unpack_from(">II", data, offset)
            offset += 8
            if not stack or offset + length > end or nameoff >= strings_size:
                raise ValueError(f"invalid property: {path}")
            start = strings + nameoff
            stop = data.index(0, start, strings + strings_size)
            name = data[start:stop].decode()
            props = nodes["/" + "/".join(stack[1:])]
            if name in props:
                raise ValueError(f"duplicate property {name}: {path}")
            props[name] = data[offset:offset + length]
            offset = (offset + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack:
                raise ValueError(f"unclosed FDT nodes: {path}")
            return nodes
        else:
            raise ValueError(f"unknown FDT token {token}: {path}")
    raise ValueError(f"missing FDT end: {path}")


def property_strings(value):
    return value.rstrip(b"\0").decode().split("\0")


def inspect_tree(path):
    nodes = read_fdt(path)
    if "/__fixups__" in nodes:
        raise ValueError(f"unresolved overlay fixups: {path}")
    phandles = {}
    for node, props in nodes.items():
        value = props.get("phandle")
        if value is None:
            continue
        handle, = struct.unpack(">I", value)
        if not handle or handle == 0xffffffff or handle in phandles:
            raise ValueError(f"invalid or duplicate phandle at {node}")
        phandles[handle] = node
    active = {}
    for node, props in nodes.items():
        parts = node.strip("/").split("/")
        ancestors = ["/"] + ["/" + "/".join(parts[:i]) for i in range(1, len(parts) + 1)]
        enabled = all(nodes[p].get("status", b"okay\0") in (b"okay\0", b"ok\0")
                      for p in ancestors)
        if enabled and "compatible" in props:
            active[node] = property_strings(props["compatible"])
    return {"nodes": len(nodes), "phandles": len(phandles),
            "model": property_strings(nodes["/"]["model"])[0],
            "msm_id": list(struct.unpack(">" + "I" * (len(nodes["/"]["qcom,msm-id"]) // 4),
                                         nodes["/"]["qcom,msm-id"])),
            "active_compatible_nodes": active}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--output", type=Path,
                        help="new output directory; defaults to WORKSPACE/artifacts/dt-TIMESTAMP")
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    subprocess.run([sys.executable, str(HERE / "prepare.py"), str(workspace),
                    "--verify-only"], check=True)
    config = json.loads((HERE / "devicetrees.json").read_text())
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = (args.output or workspace / "artifacts" / ("dt-" + stamp)).resolve()
    output.mkdir(parents=True, exist_ok=False)
    raw, merged, checked, tools = [output / name for name in ["raw", "merged", "checked", "tools"]]
    for directory in [raw, merged, checked, tools]:
        directory.mkdir()
    log_path = output / "build.log"
    print(f"Building device trees; log: {log_path}", flush=True)
    commands = []
    dependencies = set()
    with log_path.open("w") as log:
        def run(command):
            command = [str(p) for p in command]
            commands.append(command)
            log.write(json.dumps(command) + "\n")
            log.flush()
            subprocess.run(command, cwd=workspace, stdout=log, stderr=subprocess.STDOUT,
                           check=True)

        source = workspace / TOOL_PROJECT
        (tools / "version_gen.h").write_text('#define DTC_VERSION "Onyx pinned libfdt tools"\n')
        lib_sources = sorted((source / "libfdt").glob("*.c"))
        for name in ["fdtoverlay", "fdtoverlaymerge"]:
            run(["cc", "-O2", "-D_GNU_SOURCE", "-I" + str(source),
                 "-I" + str(source / "libfdt"), "-I" + str(tools),
                 source / (name + ".c"), source / "util.c", *lib_sources,
                 "-o", tools / name])
        dependencies.update(p for p in source.rglob("*") if p.is_file()
                            and p.suffix in (".c", ".h") and ".git" not in p.parts)
        pins = json.loads((HERE / "sources.json").read_text())
        clang = workspace / "prebuilts/clang/host/linux-x86" / ("clang-" + pins["common"]["clang"]) / "bin/clang"
        prebuilts = workspace / "prebuilts/kernel-build-tools/linux_musl-x86/bin"
        dtc = prebuilts / "dtc"
        includes = [workspace / KERNEL_PROJECT / "scripts/dtc/include-prefixes",
                    workspace / MODULE_PROJECT / "qcom/opensource/audio-kernel/include",
                    workspace / MODULE_PROJECT / "qcom/opensource/camera-kernel",
                    workspace / MODULE_PROJECT / "qcom/opensource/synx-kernel"]
        all_sources = sorted({s for t in config["trees"] for s in [t["source"], *t["overlays"]]})
        compiled = {}
        for name in all_sources:
            source = workspace / DT_PROJECT / name
            destination = raw / Path(name).with_suffix(".dtb")
            destination.parent.mkdir(parents=True, exist_ok=True)
            cpp, dep = destination.with_suffix(".dts.tmp"), destination.with_suffix(".d")
            run([clang, "-E", "-MMD", "-MF", dep, "-nostdinc", "-undef", "-D__DTS__",
                 *["-I" + str(p) for p in includes], "-x", "assembler-with-cpp",
                 "-o", cpp, source])
            run([dtc, "-I", "dts", "-O", "dtb", "-@", "-b", "0", "-i", source.parent,
                 *["-i" + str(p) for p in includes], "-o", destination, cpp])
            read_fdt(destination)
            deps = dep.read_text().replace("\\\n", " ").split(":", 1)[1].split()
            dependencies.update(Path(p).resolve() for p in deps)
            compiled[name] = destination
        for tree in config["trees"]:
            program = "fdtoverlaymerge" if tree["base"].endswith(".dtbo") else "fdtoverlay"
            run([tools / program, "-i", compiled[tree["source"]],
                 "-o", merged / tree["base"], *[compiled[p] for p in tree["overlays"]]])
            original_root = read_fdt(compiled[tree["source"]])["/"]
            merged_root = read_fdt(merged / tree["base"])["/"]
            for key in ["qcom,msm-id", "qcom,board-id", "qcom,pmic-id",
                        "qcom,pmic-id-size", "xiaomi,miboard-id"]:
                if original_root.get(key) != merged_root.get(key):
                    raise ValueError(f"overlay merging changed {key} in {tree['base']}")
        checks = {}
        for name in config["dtb_order"]:
            result = checked / name
            run([tools / "fdtoverlay", "-i", merged / name, "-o", result,
                 merged / config["board_overlay"]])
            # Android's compact overlay implementation is used by the boot chain.
            android_result = checked / (name + ".ufdt")
            run([prebuilts / "ufdt_apply_overlay", merged / name,
                 merged / config["board_overlay"], android_result])
            libfdt_nodes, android_nodes = read_fdt(result), read_fdt(android_result)
            # libufdt retains the base symbol table but does not publish new
            # overlay labels. These labels are compile-time linking metadata.
            libfdt_nodes.pop("/__symbols__", None)
            android_nodes.pop("/__symbols__", None)
            if libfdt_nodes != android_nodes:
                raise ValueError(f"libfdt and Android overlay results disagree for {name}")
            checks[name] = inspect_tree(result)
            sd = read_fdt(result)["/soc/sdhci@8804000"]
            if sd.get("status") != b"disabled\0":
                raise ValueError("Onyx SDHC unexpectedly enabled; port its driver before packaging")
        (output / "dtb.img").write_bytes(b"".join((merged / p).read_bytes()
                                                  for p in config["dtb_order"]))
        run([prebuilts / "mkdtboimg", "create", output / "dtbo.img",
             "--page_size=" + str(config["page_size"]), merged / config["board_overlay"]])
        if (output / "dtbo.img").stat().st_size > config["dtbo_partition_size"]:
            raise ValueError("DTBO image exceeds partition size")
        image = (output / "dtbo.img").read_bytes()
        magic, size, header_size, entry_size, count, offset, page, version = struct.unpack_from(
            ">8I", image)
        if (magic != 0xd7b7ab1e or size > len(image) or header_size != 32
                or entry_size != 32 or count != 1 or offset != 32
                or page != config["page_size"] or version != 0
                or len(image) != (size + page - 1) // page * page
                or any(image[size:])):
            raise ValueError("unexpected DTBO image table")
        blob_size, blob_offset = struct.unpack_from(">2I", image, offset)
        if (blob_offset < offset + entry_size or blob_offset + blob_size != size
                or image[blob_offset:size] != (merged / config["board_overlay"]).read_bytes()):
            raise ValueError("DTBO image does not contain the validated board overlay")
    source_hashes = {p.relative_to(workspace).as_posix(): digest(p) for p in sorted(dependencies)}
    inputs = output / "inputs"
    inputs.mkdir()
    for name in ["devicetrees.json", "sources.json", "onyx-port.xml", "dependency-lock.xml",
                 "build_devicetrees.py", "boot-parameters.txt"]:
        shutil.copyfile(HERE / name, inputs / name)
    (inputs / "source-hashes.json").write_text(json.dumps(source_hashes, indent=2) + "\n")
    # A source edit during compilation must not produce an apparently pinned archive.
    subprocess.run([sys.executable, str(HERE / "prepare.py"), str(workspace),
                    "--verify-only"], check=True)
    report = {"commands": commands, "trees": checks, "source_files": len(source_hashes),
              "dtc_warnings": len(re.findall(r"Warning \(", log_path.read_text())),
              "overlay_engines_agree": True,
              "bootloader_ids_preserved": True,
              "dtbo_table_verified": True,
              "sha256": {p.relative_to(output).as_posix(): digest(p)
                         for p in sorted(output.rglob("*")) if p.is_file()},
              "limits": ["DT compilation and overlay checks only; no boot or hardware validation.",
                         "dtb.img and dtbo.img require matching boot images and modules before flashing."]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Device-tree compilation and overlay checks passed: {output}")


if __name__ == "__main__":
    main()
