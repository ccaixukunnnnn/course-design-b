#!/bin/bash
set -euo pipefail
cd /mnt/d/OS\ experiment/course-design-b
for f in coursework/scripts/three-*.sh coursework/scripts/*perf-baseline*.sh coursework/scripts/build-three-tests.sh coursework/scripts/test-event-poll.sh; do bash -n "$f"; done
python3 -m py_compile coursework/scripts/qemu-three.py coursework/scripts/three-perf-compare.py
echo 'Shell and Python syntax checks: PASS'