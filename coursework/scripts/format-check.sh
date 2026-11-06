#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
formatter="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin/rustfmt"
"$formatter" --edition 2024 kernel/src/file/event.rs kernel/src/file/event_counter.rs coursework/tests/counter_harness.rs
"$formatter" --check --edition 2024 kernel/src/file/event.rs kernel/src/file/event_counter.rs coursework/tests/counter_harness.rs 2>&1 | tee coursework/logs/rustfmt.log
exit "${PIPESTATUS[0]}"