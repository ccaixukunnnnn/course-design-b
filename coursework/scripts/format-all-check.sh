#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
fmt="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin/rustfmt"
find src kernel/src -name '*.rs' -type f -print0 | xargs -0 "$fmt" --edition 2024 --check 2>&1 | tee coursework/logs/rustfmt-all-direct.log
exit "${PIPESTATUS[1]}"