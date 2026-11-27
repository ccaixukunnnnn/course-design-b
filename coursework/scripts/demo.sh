#!/usr/bin/env bash
# Prepared WSL environment; each run uses a private rootfs copy and QEMU.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$root"
mkdir -p .local/demo
suite=${1:-task_notifier}
case "$suite" in
    task_notifier|task_queue|deadline_queue|timerfd_periodic|worker_recovery|folder_service) ;;
    *) echo 'Usage: demo.sh [task_notifier|task_queue|deadline_queue|timerfd_periodic|worker_recovery|folder_service] [--interactive]' >&2; exit 2 ;;
esac
mode=(--mode verify --timeout 120)
if [[ $# == 2 && $suite == folder_service && $2 == --interactive ]]; then
    mode=(--interactive)
elif [[ $# -gt 1 ]]; then
    echo 'Interactive mode is available for folder_service' >&2
    exit 2
fi
cc=.local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc
"$cc" -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror "coursework/tests/$suite.c" -o ".local/demo/$suite"
python3 coursework/scripts/qemu-three.py --kernel project_riscv64-qemu-virt.bin --guest ".local/demo/$suite" "${mode[@]}"
