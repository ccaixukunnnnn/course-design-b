#!/usr/bin/env bash
# Rebuild a source snapshot with fresh Cargo extraction and build outputs.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$root"
cpus=${1:-1}
[[ $cpus =~ ^(1|2|4)$ ]] || { echo 'Expected CPU count 1, 2 or 4' >&2; exit 2; }
work=$(mktemp -d /tmp/starry-rebuild.XXXXXXXX)
evidence=$(mktemp -d "$root/coursework/logs/rebuild-${cpus}cpu-XXXXXXXX")
printf '%s\n' "$work" > "$evidence/workspace"
trap 'rc=$?; echo "$rc" > "$evidence/exit-code"' EXIT
mkdir -p "$work/project" "$work/cargo/registry"
git ls-files -z --cached --others --exclude-standard -- README.md LICENSE NOTICE Cargo.toml Cargo.lock Makefile rust-toolchain.toml rustfmt.toml src kernel make coursework/scripts coursework/tests | sort -zu > "$work/files"
tar --null -T "$work/files" -cf - | tar -xf - -C "$work/project"
xargs -0 sha256sum < "$work/files" > "$evidence/source.sha256"
cp -a .local/cargo/registry/cache .local/cargo/registry/index "$work/cargo/registry/"
cp .local/cargo/.crates.toml .local/cargo/.crates2.json "$work/cargo/"
ln -s "$root/.local/cargo/bin" "$work/cargo/bin"
export CARGO_HOME="$work/cargo" RUSTUP_HOME="$root/.local/rustup"
export RUSTUP_TOOLCHAIN=nightly-2026-02-25-x86_64-unknown-linux-gnu
export CARGO_NET_OFFLINE=true
export PATH="$RUSTUP_HOME/toolchains/$RUSTUP_TOOLCHAIN/bin:$work/cargo/bin:$root/.local/sysroot/usr/bin:$root/.local/riscv64-linux-musl-cross/bin:/usr/sbin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$root/.local/sysroot/usr/lib/x86_64-linux-gnu:$root/.local/sysroot/usr/lib/llvm-18/lib"
export LIBCLANG_PATH="$root/.local/sysroot/usr/lib/llvm-18/lib"
{ rustc -Vv; cargo -V; qemu-system-riscv64 --version; } > "$evidence/environment.txt"
printf 'Rebuild evidence %s\n' "$evidence"
(
    cd "$work/project"
    make ARCH=riscv64 NET=n SMP="$cpus" TARGET_DIR="$work/target" build
) > "$evidence/build.log" 2>&1 || { tail -40 "$evidence/build.log"; exit 1; }
cp "$work/project/project_riscv64-qemu-virt.bin" "$evidence/kernel.bin"
cp "$work/project/.axconfig.toml" "$evidence/config.toml"
sha256sum "$evidence/kernel.bin" > "$evidence/kernel.sha256"
(cd "$work/project"; sha256sum -c --quiet "$evidence/source.sha256")
printf 'PASS %s\n' "$evidence"
