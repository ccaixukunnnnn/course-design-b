#!/bin/bash
set -uo pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
mkdir -p .local/security-fixes
.local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc \
    -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror \
    coursework/tests/security_regression.c -o .local/security-fixes/guest || exit 1
python3 coursework/scripts/qemu-three.py --guest .local/security-fixes/guest --kernel project_riscv64-qemu-virt.bin --mode verify --timeout 120 2>&1 | tee coursework/logs/security-fixes-qemu.log
exit "${PIPESTATUS[0]}"
