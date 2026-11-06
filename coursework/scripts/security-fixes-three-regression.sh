#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
python3 coursework/scripts/qemu-three.py --kernel project_riscv64-qemu-virt.bin --mode verify 2>&1 | tee coursework/logs/security-fixes-three-regression.log
exit "${PIPESTATUS[0]}"