#!/bin/bash
set -euo pipefail
cd /mnt/d/OS\ experiment/course-design-b
B="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin"
"$B/rustc" --edition 2024 --test coursework/tests/mincore_range_harness.rs -o .local/tests/mincore-range-tests
.local/tests/mincore-range-tests
