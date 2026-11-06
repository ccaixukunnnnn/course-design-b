#!/usr/bin/env python3
"""Run the eventfd test with a private image and a specifically supplied kernel."""
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
parser.add_argument("--kernel", required=True, type=Path)
parser.add_argument("--expect", choices=("pass", "fail"), default="pass")
parser.add_argument("--timeout", type=int, default=90)
args = parser.parse_args()
kernel = args.kernel.resolve(strict=True)
image = ROOT / "rootfs-riscv64.img"
guest_test = ROOT / ".local/tests/eventfd-riscv64"
if not image.is_file() or not guest_test.is_file():
    sys.exit("prepare rootfs and run coursework/scripts/build-guest-test.sh first")

with tempfile.TemporaryDirectory(prefix="qemu-test-", dir=ROOT / ".local") as tmp:
    disk = Path(tmp) / "disk.img"
    shutil.copyfile(image, disk)
    # debugfs interprets its command string; reject unusual path characters.
    if any(c in str(guest_test) for c in '"\n\r'):
        sys.exit("unsupported path for debugfs")
    subprocess.run(["debugfs", "-w", "-R", f'write "{guest_test}" /eventfd-test', str(disk)], check=True)
    command = ["qemu-system-riscv64", "-machine", "virt", "-bios", "default", "-m", "1G", "-smp", "1", "-nographic", "-monitor", "none", "-kernel", str(kernel), "-device", "virtio-blk-pci,drive=disk0", "-drive", f"id=disk0,if=none,format=raw,file={disk}"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = bytearray()
    sent = False
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([process.stdout], [], [], 0.5)
            if ready:
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    break
                output.extend(chunk)
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            if not sent and b"starry:~#" in output:
                process.stdin.write(b"/eventfd-test; echo __EVENTFD_RC:$?\n")
                process.stdin.flush()
                sent = True
            match = re.search(rb"__EVENTFD_RC:(\d+)\r?\n", output)
            if match:
                code = int(match.group(1))
                ok = (code == 0 and b"eventfd semantics: PASS" in output) if args.expect == "pass" else (code != 0 and b"expected 1, got 3" in output)
                if not ok:
                    sys.exit(f"unexpected guest test result: {code}")
                print(f"\nQEMU eventfd expected {args.expect}: verified")
                break
        else:
            sys.exit("timeout waiting for guest test result")
        if not re.search(rb"__EVENTFD_RC:(\d+)\r?\n", output):
            sys.exit("QEMU exited before guest test completed")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
