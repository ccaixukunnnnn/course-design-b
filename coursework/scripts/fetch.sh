#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
export PATH=/home/chenzheyu/.cargo/bin:/usr/bin:/bin
export CARGO_HOME="$PWD/.local/cargo"
export RUSTUP_HOME=/home/chenzheyu/.rustup
export RUSTUP_TOOLCHAIN=stable
export CARGO_HTTP_TIMEOUT=30
cargo fetch --locked 2>&1 | tee coursework/logs/fetch.log
exit "${PIPESTATUS[0]}"