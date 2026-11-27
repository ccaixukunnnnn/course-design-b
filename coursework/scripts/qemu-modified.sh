#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
python3 coursework/scripts/qemu-eventfd.py --kernel project_riscv64-qemu-virt.bin --expect pass --timeout 120 2>&1 | tee coursework/logs/qemu-modified.log
exit "${PIPESTATUS[0]}"