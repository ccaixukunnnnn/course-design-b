#!/bin/bash
set -e
cd /mnt/d/OS\ experiment/course-design-b/.local
curl -fL --retry 2 --connect-timeout 20 -o riscv64-linux-musl-cross.tgz https://github.com/arceos-org/setup-musl/releases/download/prebuilt/riscv64-linux-musl-cross.tgz
tar xzf riscv64-linux-musl-cross.tgz