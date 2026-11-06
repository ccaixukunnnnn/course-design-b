#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$root"
kernel=$(realpath "${1:?Provide an SMP-enabled kernel path}")
cpus=${2:-4}
[[ $cpus =~ ^(1|2|4)$ ]] || exit 2
python3 - "$kernel" "$cpus" <<'PY'
import sys
import tomllib
from pathlib import Path
config = Path(sys.argv[1]).parent / "config.toml"
if not config.is_file():
    sys.exit("Missing adjacent config.toml; use a build-isolated.sh kernel")
with config.open("rb") as stream:
    configured = tomllib.load(stream)["plat"]["max-cpu-num"]
if configured != int(sys.argv[2]):
    sys.exit(f"Kernel configured for {configured} CPUs, requested {sys.argv[2]}")
PY
evidence=$(mktemp -d "$root/coursework/logs/extended-${cpus}cpu-XXXXXXXX")
trap 'rc=$?; echo "$rc" > "$evidence/exit-code"' EXIT
printf 'Extended verification %s\n' "$evidence"
inputs=("$kernel" rootfs-riscv64.img coursework/scripts/qemu-three.py coursework/scripts/extended-verify.sh)
for suite in task_queue event_stress epoll_regression; do
    .local/riscv64-linux-musl-cross/bin/riscv64-linux-musl-gcc -static -no-pie -std=c11 -O2 -Wall -Wextra -Werror "coursework/tests/$suite.c" -o "$evidence/$suite.bin"
    inputs+=("coursework/tests/$suite.c" "$evidence/$suite.bin")
done
sha256sum "${inputs[@]}" > "$evidence/inputs.sha256"
for suite in task_queue event_stress epoll_regression; do
    python3 coursework/scripts/qemu-three.py --kernel "$kernel" --cpus "$cpus" --guest "$evidence/$suite.bin" --mode verify > "$evidence/$suite.log" 2>&1 || { tail -30 "$evidence/$suite.log"; exit 1; }
    printf '%s PASS\n' "$suite"
done
sha256sum -c --quiet "$evidence/inputs.sha256"
echo PASS
