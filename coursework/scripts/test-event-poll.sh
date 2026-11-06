#!/bin/bash
set -euo pipefail
cd /mnt/d/OS\ experiment/course-design-b
B="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin"
"$B/rustfmt" --edition 2024 kernel/src/file/event.rs kernel/src/file/event_poll.rs kernel/src/file/mod.rs kernel/src/syscall/sys.rs kernel/src/syscall/mod.rs kernel/src/syscall/task/ctl.rs coursework/tests/event_poll_harness.rs
"$B/rustc" --edition 2024 --test coursework/tests/event_poll_harness.rs -o .local/tests/event-poll-tests
.local/tests/event-poll-tests