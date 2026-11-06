#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b || exit 1
if [ -e make/disk.img ]; then
    echo 'Refusing to overwrite make/disk.img' >&2
    exit 1
fi
make ARCH=riscv64 rootfs 2>&1 | tee coursework/logs/rootfs.log
exit "${PIPESTATUS[0]}"
