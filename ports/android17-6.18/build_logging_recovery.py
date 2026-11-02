#!/usr/bin/env python3
"""Build an offline rescue recovery with an init-level pstore-to-metadata copy."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

HERE = Path(__file__).resolve().parent


def entries(data):
    result = {}
    pos = 0
    while True:
        start = pos
        assert data[pos:pos + 6] == b"070701"
        fields = [int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16) for i in range(13)]
        name = data[pos + 110:pos + 110 + fields[11] - 1].decode()
        pos = (pos + 110 + fields[11] + 3) & ~3
        payload = data[pos:pos + fields[6]]
        pos = (pos + fields[6] + 3) & ~3
        if name == "TRAILER!!!":
            return result
        assert name not in result
        result[name] = (fields, payload, data[start:pos])


def record(name, fields, payload):
    fields = list(fields)
    encoded = name.encode() + b"\0"
    fields[6], fields[11] = len(payload), len(encoded)
    data = b"070701" + b"".join(f"{x:08x}".encode() for x in fields) + encoded
    data += b"\0" * (-len(data) % 4)
    data += payload
    return data + b"\0" * (-len(data) % 4)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("backup", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--rom-root", type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    tools = a.rom_root / "out/host/linux-x86/bin"
    def run(name, *args):
        return subprocess.check_output([str(tools / name), *map(str, args)])
    source = a.backup / "recovery_a.img"
    manifest = json.loads((a.backup / "backup-manifest.json").read_text())
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    assert sha(source) == manifest["images"]["recovery"]["sha256"]
    unpack = a.output / "unpack"
    args = shlex.split(run("unpack_bootimg", "--boot_img", source, "--out", unpack,
                           "--format", "mkbootimg").decode())
    assert (unpack / "kernel").stat().st_size == 0, "Expect existing recovery to use boot's kernel"
    original = entries(run("lz4", "-d", "-c", unpack / "ramdisk"))
    target = "system/etc/init/hw/init.rc"
    added = "system/etc/init/onyx-pstore.rc"
    assert added not in original
    fields, payload, _ = original[target]
    changed = b"import /system/etc/init/onyx-pstore.rc\n" + payload
    blob = b"".join(record(n, f, changed) if n == target else raw
                    for n, (f, content, raw) in original.items())
    inode = max(x[0][0] for x in original.values()) + 1
    newfields = [inode, 0o100644, 0, 0, 1, fields[5], 0, 0, 0, 0, 0, 0, 0]
    blob += record(added, newfields, (HERE / "recovery-pstore.rc").read_bytes())
    blob += record("TRAILER!!!", [0] * 13, b"")
    blob += b"\0" * (-len(blob) % 512)
    cpio = a.output / "ramdisk.cpio"
    cpio.write_bytes(blob)
    compressed = a.output / "ramdisk.lz4"
    run("lz4", "-l", "-12", cpio, compressed)
    args[args.index("--ramdisk") + 1] = str(compressed)
    image = a.output / "recovery.img"
    run("mkbootimg", *args, "--output", image)
    info = run("avbtool", "info_image", "--image", source).decode()
    salt = re.search(r"Salt:\s*(\S+)", info).group(1)
    rollback = re.search(r"Rollback Index:\s*(\d+)", info).group(1)
    algorithm = re.search(r"Algorithm:\s*(\S+)", info).group(1)
    assert algorithm == "SHA256_RSA2048"
    command = ["add_hash_footer", "--image", image, "--partition_name", "recovery",
               "--partition_size", source.stat().st_size, "--algorithm", algorithm,
               "--key", a.rom_root / "external/avb/test/data/testkey_rsa2048.pem",
               "--rollback_index", rollback, "--salt", salt]
    for key, value in re.findall(r"Prop: (\S+) -> '([^']*)'", info):
        command += ["--prop", key + ":" + value]
    run("avbtool", *command)
    run("avbtool", "verify_image", "--image", image)
    final_info = run("avbtool", "info_image", "--image", image).decode()
    assert re.search(r"Public key \(sha1\):\s*(\S+)", info).group(1) == re.search(
        r"Public key \(sha1\):\s*(\S+)", final_info).group(1)
    check = a.output / "check"
    run("unpack_bootimg", "--boot_img", image, "--out", check)
    after = entries(run("lz4", "-d", "-c", check / "ramdisk"))
    assert set(after) == set(original) | {added}
    assert after[target][1] == changed
    assert after[added][1] == (HERE / "recovery-pstore.rc").read_bytes()
    for name, (_, _, raw) in original.items():
        if name != target:
            assert after[name][2] == raw, name
    assert image.stat().st_size == source.stat().st_size
    report = dict(status="PASS: offline image audit; recovery boot untested",
                  source_sha256=sha(source), sha256=sha(image), image=str(image),
                  preserved_raw_cpio_records=len(original) - 1,
                  changed_records=[target], added_records=[added],
                  boot_kernel="unchanged: recovery uses current boot partition kernel",
                  destination="/metadata/ota/onyx-boot-logs/rescue-${ro.boottime.init}",
                  limitations=["No record if firmware clears RAM or crash predates ramoops",
                               "Missing record copy failures are expected; inspect init errors",
                               "Requires writable metadata; no format or userdata mount"])
    (a.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
