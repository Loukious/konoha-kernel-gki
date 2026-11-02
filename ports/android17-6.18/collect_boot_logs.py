#!/usr/bin/env python3
"""Copy Onyx kernel evidence to a private host directory without clearing logs."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import time

from collect_rawdump_logs import collect as collect_rawdump


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--adb", default="/mnt/d/platform-tools/adb.exe")
    p.add_argument("--serial", default="5493f3c6")
    p.add_argument("--wait-seconds", type=int, default=0)
    p.add_argument("--recovery", action="store_true", help="Use root recovery shell without su")
    p.add_argument("--include-oops", action="store_true", help="Also read the OEM 16 MiB log partition")
    p.add_argument("--skip-rawdump", action="store_true", help="Skip the durable Qualcomm kernel-log sections")
    a = p.parse_args()
    deadline = time.monotonic() + a.wait_seconds
    adb = [a.adb, "-s", a.serial]
    while True:
        result = subprocess.run(adb + ["get-state"], capture_output=True, timeout=5)
        accepted_states = (b"device", b"recovery") if a.recovery else (b"device",)
        if result.returncode == 0 and result.stdout.strip() in accepted_states:
            break
        if time.monotonic() >= deadline:
            raise SystemExit("ADB unavailable; remain in the first rescue boot and collect before reboot")
        time.sleep(2)
    a.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    commands = {
        # Archive pstore FIRST. Later steps never unlink or clear it.
        "pstore.tar": "tar -C /sys/fs/pstore -cf - .",
        "android-archives.tar": "tar -C /data/adb -cf - onyx-boot-logs",
        "rescue-archives.tar": "tar -C /metadata/ota -cf - onyx-boot-logs",
        "kernel-current.txt": "dmesg",
        "state.txt": "uname -a; cat /proc/sys/kernel/random/boot_id; getprop sys.boot_completed; getprop ro.boot.bootreason; getprop sys.boot.reason; cat /proc/cmdline; ls -la /sys/fs/pstore; for p in /sys/module/ramoops/parameters/*; do echo $p; cat $p; done",
        "live.dtb": "cat /sys/firmware/fdt",
        "config.gz": "cat /proc/config.gz",
    }
    if a.include_oops:
        commands["oops.img"] = "cat /dev/block/by-name/oops"
    receipt = {"serial": a.serial, "timestamp": datetime.now(timezone.utc).isoformat(), "files": {}}
    for name, cmd in commands.items():
        if not a.recovery:
            cmd = "su -c '" + cmd + "'"
        path = a.output / name
        with path.open("wb") as f:
            result = subprocess.run(adb + ["exec-out", cmd], stdout=f, stderr=subprocess.PIPE, timeout=45)
        path.chmod(0o600)
        receipt["files"][name] = {"returncode": result.returncode,
            "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "stderr": result.stderr.decode(errors="replace")}
        item = receipt["files"][name]
        item["status"] = "PASS" if result.returncode == 0 else "unavailable"
        if name.endswith(".tar"):
            # Android exec-out can return success while tar reports a missing
            # optional directory in its output. Check the archive itself.
            try:
                with tarfile.open(path) as archive:
                    members = archive.getmembers()
                    for member in members:
                        if member.isfile():
                            stream = archive.extractfile(member)
                            while stream.read(1024 * 1024):
                                pass
                item["archive_files"] = sum(m.isfile() for m in members)
            except (tarfile.TarError, OSError, EOFError) as error:
                item.update(status="unavailable", validation_error=str(error))
        if name == "pstore.tar" and not a.skip_rawdump:
            try:
                rawdump = collect_rawdump(a.output / 'rawdump', adb, a.recovery)
            except (ValueError, OSError, subprocess.SubprocessError) as error:
                receipt['rawdump'] = {'status': 'unavailable', 'reason': str(error)}
            else:
                receipt['rawdump'] = {'status': 'PASS', 'receipt': rawdump}
    (a.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
