#!/bin/bash
set -o pipefail
real='/mnt/d/OS experiment/course-design-b'
shortdir=$(mktemp -d /tmp/course-b.XXXXXXXX) || exit 1
ln -s "$real" "$shortdir/project" || exit 1
B="$shortdir/project"
export PATH="$B/.local/sysroot/usr/bin:$B/.local/riscv64-linux-musl-cross/bin:$B/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$B/.local/sysroot/usr/lib/x86_64-linux-gnu:$B/.local/sysroot/usr/lib/llvm-18/lib"
export LIBCLANG_PATH="$B/.local/sysroot/usr/lib/llvm-18/lib"
export CARGO_HOME="$B/.local/cargo"
export CARGO_TARGET_DIR="$B/.local/target-short-baseline"
export ARCH=riscv64
cd "$B/.local/baseline" || exit 1
cargo check --locked --offline --target riscv64gc-unknown-none-elf --features qemu 2>&1 | tee "$B/coursework/logs/baseline-local-deps.log"
exit "${PIPESTATUS[0]}"