#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
B="$PWD"
export PATH="$B/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin:/usr/bin:/bin"
export CARGO_HOME="$B/.local/cargo"
export CARGO_TARGET_DIR="$B/.local/target-baseline"
cd "$B/.local/baseline" || exit 1
cargo check --locked --offline --target riscv64gc-unknown-none-elf --features qemu 2>&1 | tee "$B/coursework/logs/baseline-nightly-check.log"
exit "${PIPESTATUS[0]}"