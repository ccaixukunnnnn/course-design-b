#!/usr/bin/env bash
# Prepared WSL environment; each run uses a private rootfs copy and QEMU.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$root"
mkdir -p .local/demo
suite=${1:-task_notifier}
case "$suite" in
    task_notifier|task_queue) ;;
    *) echo 'Usage: demo.sh [task_notifier|task_queue]' >&2; exit 2 ;;
esac
cc=.local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc
"$cc" -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror "coursework/tests/$suite.c" -o ".local/demo/$suite"
python3 coursework/scripts/qemu-three.py --kernel project_riscv64-qemu-virt.bin --guest ".local/demo/$suite" --mode verify --timeout 120
