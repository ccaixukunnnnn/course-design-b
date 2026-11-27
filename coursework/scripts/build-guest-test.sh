#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
mkdir -p .local/tests
.local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc -static -no-pie -std=c11 -Wall -Wextra -Werror coursework/tests/eventfd_semantics.c -o .local/tests/eventfd-riscv64 2>&1 | tee coursework/logs/cross-test-build.log
exit "${PIPESTATUS[0]}"
