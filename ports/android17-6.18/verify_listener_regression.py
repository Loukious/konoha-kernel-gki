#!/usr/bin/env python3
"""Show that the listener shutdown harness rejects the pinned pre-fix body."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import tempfile
from verify_gpu import function

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("workspace", type=Path)
ap.add_argument("--output", type=Path)
a = ap.parse_args()
here = Path(__file__).resolve().parent
path = "qcom/opensource/mm-drivers/hw_fence/src/hw_fence_drv_utils.c"
pin = next(i for i in json.loads((here / "gpu-imports.json").read_text()) if i["path"] == path)
source = subprocess.check_output(["git", "-C", str(a.workspace / pin["project"]),
                                  "cat-file", "blob", pin["blob"]], text=True)
body = function(source, "hw_fence_soccp_listener")
harness = (here / "tests/gpu_hwfence_listener.c").read_text()
with tempfile.TemporaryDirectory(prefix="onyx-gpu-listener-regression-") as temp:
    d = Path(temp)
    (d / "negative.c").write_text(harness.replace("/* STAGED_DEFINITIONS */", body))
    subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-pthread",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer", str(d / "negative.c"),
                    "-o", str(d / "negative")], check=True)
    def no_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run = subprocess.run([str(d / "negative")], capture_output=True, text=True,
                         timeout=10, preexec_fn=no_core)
    assert run.returncode and "pthread_timedjoin_np" in run.stderr, run.stderr
report = {"pinned_blob": pin["blob"], "listener_sha256": hashlib.sha256(body.encode()).hexdigest(),
          "expected_result": "rejected: original listener does not stop while idle",
          "observed_exit": run.returncode,
          "harness_sha256": hashlib.sha256(harness.encode()).hexdigest()}
result = json.dumps(report, indent=2) + "\n"
if a.output:
    a.output.write_text(result)
print(result, end="")
