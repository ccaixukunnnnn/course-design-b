#!/usr/bin/env python3
"""Retain three independent boots of request/ack notification measurements."""
import argparse
import hashlib
import json
import math
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--kernel", type=Path, default=root / "project_riscv64-qemu-virt.bin")
args = parser.parse_args()
evidence = Path(tempfile.mkdtemp(prefix="notify-perf-", dir=root / "coursework/logs"))
guest = evidence / "notify-bench.bin"
subprocess.run([str(root / ".local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc"),
                "-static", "-no-pie", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                str(root / "coursework/tests/notify_bench.c"), "-o", str(guest)], check=True)
inputs = [args.kernel.resolve(), guest, root / "rootfs-riscv64.img",
          root / "coursework/tests/notify_bench.c", Path(__file__).resolve(),
          root / "coursework/scripts/qemu-three.py"]

def fingerprints():
    result = {}
    for path in inputs:
        with path.open("rb") as stream:
            result[str(path)] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result

hashes = fingerprints()
report = {"inputs_sha256": hashes, "cpus": 1, "warmup_per_case": 32,
          "samples_per_case": 256, "measurement": "parent request to child acknowledgement RTT",
          "qemu": subprocess.check_output(["qemu-system-riscv64", "--version"], text=True),
          "runs": []}
for boot in range(1, 4):
    log = evidence / f"boot-{boot}.log"
    with log.open("wb") as output:
        subprocess.run([sys.executable, str(root / "coursework/scripts/qemu-three.py"),
                        "--kernel", str(args.kernel.resolve()), "--guest", str(guest),
                        "--mode", "verify"], stdout=output, stderr=subprocess.STDOUT, check=True)
    rows = re.findall(r"RTT mode=(normal|semaphore) batch=(1|16) sample=(\d+) ns=(\d+)", log.read_text())
    if len(rows) != 1024:
        raise RuntimeError(f"Expected 1024 samples in {log}")
    for mode, batch in [(m, b) for m in ("normal", "semaphore") for b in (1, 16)]:
        selected = [(int(i), int(ns)) for m, b, i, ns in rows if m == mode and int(b) == batch]
        if sorted(i for i, _ in selected) != list(range(256)) or any(ns <= 0 for _, ns in selected):
            raise RuntimeError(f"Invalid sample identities or duration in {log}")
        values = sorted(ns for _, ns in selected)
        report["runs"].append({"boot": boot, "mode": mode, "batch": batch,
                               "median_ns": statistics.median(values),
                               "p95_ns": values[math.ceil(len(values) * .95) - 1],
                               "p99_ns": values[math.ceil(len(values) * .99) - 1],
                               "tasks_per_timed_second": len(values) * batch * 1e9 / sum(values),
                               "raw_log": log.name})
    print(f"Boot {boot}/3 complete", flush=True)
if hashes != fingerprints():
    raise RuntimeError("Measurement inputs changed")
report["summary"] = []
for mode in ("normal", "semaphore"):
    for batch in (1, 16):
        runs = [r for r in report["runs"] if r["mode"] == mode and r["batch"] == batch]
        report["summary"].append({"mode": mode, "batch": batch, **{
            field: statistics.median(r[field] for r in runs)
            for field in ("median_ns", "p95_ns", "p99_ns", "tasks_per_timed_second")}})
(evidence / "results.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report["summary"], indent=2))
print(evidence)
