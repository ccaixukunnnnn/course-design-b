#!/bin/bash
set -euo pipefail
cd /mnt/d/OS\ experiment/course-design-b
mkdir -p .local/tests
.local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc \
    -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror \
    coursework/tests/three_aspects.c -o .local/tests/three-aspects-riscv64
file .local/tests/three-aspects-riscv64
