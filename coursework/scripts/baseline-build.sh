#!/bin/bash
set -o pipefail
B='/mnt/d/OS experiment/course-design-b'
T=$(cat "$B/.local/cache-tmp-path.txt") || exit 1
case "$T" in /tmp/starry-course-b-cache.*) ;; *) exit 1;; esac
P="$T/project"
[ -L "$P" ] || ln -s "$B" "$P"
[ -L "$T/cargo/bin" ] || ln -s "$P/.local/cargo/bin" "$T/cargo/bin"
cp "$P/.local/cargo/.crates.toml" "$P/.local/cargo/.crates2.json" "$T/cargo/"
export PATH="$T/cargo/bin:$P/.local/sysroot/usr/bin:$P/.local/riscv64-linux-musl-cross/bin:$P/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$P/.local/sysroot/usr/lib/x86_64-linux-gnu:$P/.local/sysroot/usr/lib/llvm-18/lib"
export LIBCLANG_PATH="$P/.local/sysroot/usr/lib/llvm-18/lib"
export CARGO_HOME="$T/cargo"
export CARGO_TARGET_DIR="$P/.local/target-build-baseline"
export ARCH=riscv64
cd "$P/.local/baseline" || exit 1
make ARCH=riscv64 NET=n build 2>&1 | tee "$P/coursework/logs/baseline-make.log"
exit "${PIPESTATUS[0]}"