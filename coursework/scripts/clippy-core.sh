#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
C="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin/clippy-driver"
"$C" --edition 2024 --test coursework/tests/counter_harness.rs -D warnings -o .local/tests/counter-clippy 2>&1 | tee coursework/logs/clippy-core.log
exit "${PIPESTATUS[0]}"