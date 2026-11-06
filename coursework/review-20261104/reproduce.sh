#!/bin/bash
# These intentionally exercise kernel failure paths. Use the isolated QEMU only.
set -uo pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
case "${1:-}" in
  credentials) src=review_cases.c; mode=probe ;;
  getrandom)   src=review_cases.c; mode=verify ;;
  mincore)     src=mincore_cases.c; mode=verify ;;
  wake)        src=wake_cases.c; mode=probe ;;
  random)      src=random_cases.c; mode=probe ;;
  *) echo 'usage: bash coursework/review-20261104/reproduce.sh {credentials|getrandom|mincore|wake|random}' >&2; exit 2 ;;
esac
test -f rootfs-riscv64.img && test -f project_riscv64-qemu-virt.bin || {
  echo 'Direction B rootfs and built kernel are required.' >&2; exit 1;
}
dir=$(mktemp -d "$PWD/.local/review-run-XXXXXXXX") || exit 1
.local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc \
  -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror \
  "coursework/review-20261104/$src" -o "$dir/guest" || exit 1
echo "Review output: $dir/run.log"
python3 coursework/scripts/qemu-three.py --guest "$dir/guest" --kernel project_riscv64-qemu-virt.bin --mode "$mode" --timeout 20 2>&1 | tee "$dir/run.log"
exit "${PIPESTATUS[0]}"
