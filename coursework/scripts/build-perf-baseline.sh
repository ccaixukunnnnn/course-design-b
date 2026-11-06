#!/bin/bash
set -euo pipefail
B='/mnt/d/OS experiment/course-design-b'
T=$(cat "$B/.local/cache-tmp-path.txt")
case "$T" in /tmp/starry-course-b-cache.*) ;; *) echo 'unexpected cache path' >&2; exit 1;; esac
P="$T/project"
test -d "$P/.local/perf-baseline" || { echo 'run prepare-perf-baseline.sh first' >&2; exit 1; }
export PATH="$T/cargo/bin:$P/.local/sysroot/usr/bin:$P/.local/riscv64-linux-musl-cross/bin:$P/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$P/.local/sysroot/usr/lib/x86_64-linux-gnu:$P/.local/sysroot/usr/lib/llvm-18/lib"
export LIBCLANG_PATH="$P/.local/sysroot/usr/lib/llvm-18/lib"
export CARGO_HOME="$T/cargo" ARCH=riscv64
cd "$P/.local/perf-baseline"
make ARCH=riscv64 NET=n TARGET_DIR="$P/.local/baseline/target" build 2>&1 | tee "$P/coursework/logs/rebuilt-perf-baseline.log"
mkdir -p "$P/.local/pre-three"
if [ -e "$P/.local/pre-three/kernel.bin" ]; then
    echo 'Existing measured baseline kernel retained; rebuilt binary is in .local/perf-baseline/'
else
    cp perf-baseline_riscv64-qemu-virt.bin "$P/.local/pre-three/kernel.bin"
fi
