#!/usr/bin/env bash
# Offline acceptance for the prepared Ubuntu/WSL x86_64 build environment.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$root"
mkdir -p coursework/logs .local
evidence=$(mktemp -d "$root/coursework/logs/accept-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")
echo "验收记录 $evidence"
trap 'rc=$?; if ((rc)); then echo "FAIL $rc"; else echo PASS; fi > "$evidence/status"' EXIT
exec 9>"$root/.local/accept.lock"
flock -n 9 || { echo 'Another acceptance run is active' >&2; exit 1; }
run() {
    local step=$1
    shift
    printf '%s ... ' "$step"
    local rc=0
    { printf 'command: '; printf '%q ' "$@"; printf '\n'; "$@"; } > "$evidence/$step.log" 2>&1 || rc=$?
    printf '%s\t%d\n' "$step" "$rc" >> "$evidence/steps.tsv"
    echo "$rc"
    if ((rc)); then tail -30 "$evidence/$step.log" >&2; exit "$rc"; fi
}
nightly=nightly-2026-02-25-x86_64-unknown-linux-gnu
for required in ".local/rustup/toolchains/$nightly/bin/rustc" .local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc .local/cargo/bin/axconfig-gen rootfs-riscv64.img; do
    [[ -f $required ]] || { echo "Missing $required (see coursework/README.md)" >&2; exit 1; }
done
cache=$(cat .local/cache-tmp-path.txt 2>/dev/null || true)
if [[ ! $cache =~ ^/tmp/starry-course-b-cache\.[[:alnum:]]+$ || ! -d $cache/cargo/registry || $(realpath "$cache/project" 2>/dev/null) != "$root" ]]; then
    run cache bash coursework/scripts/create-temporary-cache.sh
    cache=$(cat .local/cache-tmp-path.txt)
    ln -s "$root" "$cache/project"
fi
project="$cache/project"
[[ -e $cache/cargo/bin ]] || ln -s "$project/.local/cargo/bin" "$cache/cargo/bin"
[[ $(realpath "$cache/cargo/bin") == "$root/.local/cargo/bin" ]] || { echo 'Wrong build-tools directory' >&2; exit 1; }
cp "$project/.local/cargo/.crates.toml" "$project/.local/cargo/.crates2.json" "$cache/cargo/"
cd "$project"
export CARGO_HOME="$cache/cargo" RUSTUP_HOME="$project/.local/rustup" RUSTUP_TOOLCHAIN="$nightly"
export CARGO_NET_OFFLINE=true ARCH=riscv64
export PATH="$project/.local/rustup/toolchains/$nightly/bin:$cache/cargo/bin:$project/.local/sysroot/usr/bin:$project/.local/riscv64-linux-musl-cross/bin:/usr/sbin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$project/.local/sysroot/usr/lib/x86_64-linux-gnu:$project/.local/sysroot/usr/lib/llvm-18/lib"
export LIBCLANG_PATH="$project/.local/sysroot/usr/lib/llvm-18/lib"
{
    git rev-parse HEAD
    rustc -Vv
    cargo -V
    qemu-system-riscv64 --version
} > "$evidence/environment.txt"
git ls-files -z --cached --others --exclude-standard -- README.md Cargo.toml Cargo.lock Makefile rust-toolchain.toml rustfmt.toml src kernel make .github/workflows coursework/tests coursework/scripts | sort -zu | xargs -0 sha256sum > "$evidence/source.sha256"
run format rustfmt --edition 2024 --check src/main.rs kernel/src/lib.rs coursework/tests/*_harness.rs
work=$(mktemp -d "$project/.local/accept-bin-XXXXXXXX")
for harness in coursework/tests/*_harness.rs; do
    name=$(basename "$harness" _harness.rs)
    run "compile-$name" rustc --edition 2024 --test "$harness" -o "$work/$name"
    run "test-$name" "$work/$name"
done
run clippy cargo clippy --workspace --locked --offline --target-dir "$project/.local/target-short-baseline" --target riscv64gc-unknown-none-elf --features qemu -- -D warnings -A unused
run build make ARCH=riscv64 NET=n TARGET_DIR="$project/.local/baseline/target" build
cp project_riscv64-qemu-virt.bin "$work/kernel.bin"
suites=(three_aspects security_regression task_notifier epoll_regression task_queue event_stress)
for suite in "${suites[@]}"; do
    run "compile-$suite" riscv64-linux-musl-gcc -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror "coursework/tests/$suite.c" -o "$work/$suite"
done
inputs=(project_riscv64-qemu-virt.bin "$work/kernel.bin" rootfs-riscv64.img)
for suite in "${suites[@]}"; do inputs+=("$work/$suite"); done
sha256sum "${inputs[@]}" > "$evidence/inputs.sha256"
for suite in "${suites[@]}"; do
    run "qemu-$suite" python3 coursework/scripts/qemu-three.py --kernel "$work/kernel.bin" --guest "$work/$suite" --mode verify
done
run verify-inputs sha256sum --check --quiet "$evidence/inputs.sha256"
run verify-source sha256sum --check --quiet "$evidence/source.sha256"
