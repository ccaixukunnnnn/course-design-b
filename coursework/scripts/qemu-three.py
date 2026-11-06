#!/usr/bin/env python3
"""Run a test in a private StarryOS RISC-V QEMU and stop only that QEMU."""
import argparse
import os
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--kernel", type=Path, required=True)
parser.add_argument("--guest", type=Path, default=ROOT / ".local/tests/three-aspects-riscv64")
parser.add_argument("--mode", choices=("probe", "verify", "bench"), required=True)
parser.add_argument("--timeout", type=int, default=180)
parser.add_argument("--cpus", type=int, choices=(1, 2, 4), default=1)
args = parser.parse_args()
kernel = args.kernel.resolve(strict=True)
test = args.guest.resolve(strict=True)
image = ROOT / "rootfs-riscv64.img"
if not test.is_file() or not image.is_file():
    sys.exit("Build the guest test and prepare the original rootfs first")
if any(c in str(test) for c in '"\n\r'):
    sys.exit("Unsupported path for debugfs")

with tempfile.TemporaryDirectory(prefix="three-qemu-", dir=ROOT / ".local") as tmp:
    disk = Path(tmp) / "disk.img"
    shutil.copyfile(image, disk)
    subprocess.run(["debugfs", "-w", "-R", f'write "{test}" /three-test', str(disk)], check=True)
    # debugfs can report a failed write on stderr and still exit successfully.
    copied = Path(tmp) / "injected"
    subprocess.run(["debugfs", "-R", "dump /three-test injected", str(disk)], cwd=tmp, check=True)
    if copied.read_bytes() != test.read_bytes():
        sys.exit("Injected guest differs from the compiled test")
    cmd = [
        "qemu-system-riscv64", "-machine", "virt", "-bios", "default", "-m", "1G",
        "-smp", str(args.cpus), "-nographic", "-monitor", "none", "-kernel", str(kernel),
        "-device", "virtio-blk-pci,drive=disk0", "-drive",
        f"id=disk0,if=none,format=raw,file={disk}",
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = bytearray()
    sent = False
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([proc.stdout], [], [], 0.5)
            if ready:
                chunk = os.read(proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                output.extend(chunk)
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            if not sent and b"starry:~#" in output:
                proc.stdin.write(f"/three-test {args.mode}; echo __THREE_RC:$?\n".encode())
                proc.stdin.flush()
                sent = True
            match = re.search(rb"__THREE_RC:(\d+)\r?\n", output)
            if match:
                if int(match.group(1)) != 0:
                    sys.exit(f"Guest returned {match.group(1).decode()}")
                if args.mode == "verify" and b"VERIFY: PASS" not in output:
                    sys.exit("Missing verification marker")
                if args.mode == "bench" and (b"BENCH: PASS" not in output or len(re.findall(rb"BENCH round=\d+ pairs=\d+ elapsed_ns=\d+", output)) != 7):
                    sys.exit("Missing benchmark samples")
                print(f"\nQEMU {args.mode}: PASS")
                break
        else:
            sys.exit("Timed out waiting for QEMU guest")
        if not re.search(rb"__THREE_RC:(\d+)\r?\n", output):
            sys.exit("Guest did not complete the test")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
