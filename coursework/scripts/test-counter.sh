#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
export PATH="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin:/usr/bin:/bin"
mkdir -p .local/tests
rustc --edition 2024 --test coursework/tests/counter_harness.rs -o .local/tests/counter-tests && .local/tests/counter-tests 2>&1 | tee coursework/logs/counter-host.log
exit "${PIPESTATUS[0]}"
