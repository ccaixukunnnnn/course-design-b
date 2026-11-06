#!/usr/bin/env python3
"""Compare two QEMU kernels in alternating order; retain raw guest samples."""
import datetime
import hashlib
import json
import platform
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / ".local/pre-three/kernel.bin"
NEW = ROOT / "project_riscv64-qemu-virt.bin"
TEST = ROOT / ".local/tests/three-aspects-riscv64"
RUNNER = ROOT / "coursework/scripts/qemu-three.py"
IMAGE = ROOT / "rootfs-riscv64.img"
LOGS = ROOT / "coursework/logs"
inputs = {"baseline": BASE, "modified": NEW, "test": TEST, "rootfs": IMAGE, "runner": RUNNER}
for path in inputs.values():
    if not path.is_file():
        sys.exit(f"Missing input: {path}")


def digest(path):
    with path.open("rb") as src:
        return hashlib.file_digest(src, "sha256").hexdigest()


LOGS.mkdir(parents=True, exist_ok=True)
timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
fingerprints = {name: digest(path) for name, path in inputs.items()}
sequence = ["base", "new", "new", "base", "base", "new"]
results = []
for index, name in enumerate(sequence, 1):
    kernel = BASE if name == "base" else NEW
    output = LOGS / f"three-perf-{timestamp}-{index}-{name}.log"
    print(f"[{index}/6] {name}; log: {output.name}", flush=True)
    proc = subprocess.run(
        [sys.executable, str(RUNNER), "--kernel", str(kernel), "--guest", str(TEST), "--mode", "bench"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300,
    )
    output.write_bytes(proc.stdout)
    if proc.returncode:
        sys.exit(f"QEMU run failed ({proc.returncode}); see {output}")
    samples = [int(n) for n in re.findall(rb"BENCH round=\d+ pairs=20000 elapsed_ns=(\d+)", proc.stdout)]
    if len(samples) != 7:
        sys.exit(f"Unexpected sample count in {output}")
    results.append({"order": index, "kernel": name, "elapsed_ns": samples,
                    "median_ns_per_pair": statistics.median(samples) / 20000,
                    "log": str(output.relative_to(ROOT))})

base = statistics.median(r["median_ns_per_pair"] for r in results if r["kernel"] == "base")
new = statistics.median(r["median_ns_per_pair"] for r in results if r["kernel"] == "new")
if fingerprints != {name: digest(path) for name, path in inputs.items()}:
    sys.exit("An input changed during the comparison; raw logs were retained")
cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")), "unknown")
qemu = subprocess.check_output(["qemu-system-riscv64", "--version"], text=True).splitlines()[0]
report = {
    "timestamp_utc": timestamp,
    "host": platform.platform(), "host_cpu": cpu, "qemu": qemu,
    "guest": "RISC-V virt, 1 hart, 1 GiB, TCG, no network",
    "measurement": "guest CLOCK_MONOTONIC, 3000 warmup pairs then 7 x 20000 eventfd nonblocking write/read pairs per boot",
    "baseline_kernel_sha256": fingerprints["baseline"], "modified_kernel_sha256": fingerprints["modified"],
    "test_sha256": fingerprints["test"], "runs": results,
    "rootfs_sha256": fingerprints["rootfs"], "runner_sha256": fingerprints["runner"],
    "baseline_median_of_boot_medians_ns_per_pair": base,
    "modified_median_of_boot_medians_ns_per_pair": new,
    "time_reduction_percent": (base - new) / base * 100,
}
summary = LOGS / f"three-perf-{timestamp}.json"
summary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, ensure_ascii=False, indent=2))
print(f"Summary: {summary}")
