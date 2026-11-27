#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
export PATH="$PWD/.local/rustup/toolchains/nightly-2026-02-25-x86_64-unknown-linux-gnu/bin:$PWD/.local/cargo/bin:/usr/bin:/bin"
export CARGO_HOME="$PWD/.local/cargo"
export CARGO_TARGET_DIR="$PWD/.local/install-target"
for package in cargo-axplat axconfig-gen cargo-binutils; do
  cargo install --locked "$package" || exit $?
done