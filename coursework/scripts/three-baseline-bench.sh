#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
python3 coursework/scripts/qemu-three.py --kernel .local/pre-three/kernel.bin --mode bench 2>&1 | tee coursework/logs/three-baseline-bench-1.log
exit "${PIPESTATUS[0]}"