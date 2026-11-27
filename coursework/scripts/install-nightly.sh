#!/bin/bash
set -u
cd /mnt/d/OS\ experiment/course-design-b || exit 1
export RUSTUP_HOME="$PWD/.local/rustup"
export CARGO_HOME="$PWD/.local/cargo"
# Reuse the installed rustup executable, but place the toolchain in this project.
/home/chenzheyu/.cargo/bin/rustup toolchain install nightly-2026-02-25 \
  --profile minimal --component rust-src --component llvm-tools \
  --component rustfmt --component clippy --target riscv64gc-unknown-none-elf
status=$?
bin="$RUSTUP_HOME/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin"
if [ "$status" -ne 0 ]; then
    echo "rustup returned $status; check that all required components were installed" >&2
    exit "$status"
fi
"$bin/rustc" --version
