#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
export PATH=/home/chenzheyu/.cargo/bin:/usr/bin:/bin
export CARGO_HOME="$PWD/.local/cargo"
export CARGO_TARGET_DIR="$PWD/.local/target"
export RUSTUP_TOOLCHAIN=stable
export RUSTUP_HOME=/home/chenzheyu/.rustup
cargo check --locked --offline --target riscv64gc-unknown-none-elf --features qemu 2>&1 | tee coursework/logs/baseline-offline.log
exit "${PIPESTATUS[0]}"