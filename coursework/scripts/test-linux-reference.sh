#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
mkdir -p .local/tests
cc -std=c11 -Wall -Wextra -Werror coursework/tests/eventfd_semantics.c -o .local/tests/eventfd-host && .local/tests/eventfd-host 2>&1 | tee coursework/logs/eventfd-linux-host.log
exit "${PIPESTATUS[0]}"