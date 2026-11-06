#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
python3 coursework/scripts/qemu-three.py --kernel .local/perf-baseline/perf-baseline_riscv64-qemu-virt.bin --mode probe 2>&1 | tee coursework/logs/rebuilt-perf-baseline-probe.log
exit "${PIPESTATUS[0]}"