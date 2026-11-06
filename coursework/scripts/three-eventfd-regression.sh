#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
python3 coursework/scripts/qemu-eventfd.py --kernel project_riscv64-qemu-virt.bin --expect pass 2>&1 | tee coursework/logs/three-eventfd-regression.log
exit "${PIPESTATUS[0]}"