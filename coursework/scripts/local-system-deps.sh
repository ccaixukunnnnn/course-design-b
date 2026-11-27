#!/bin/bash
set -e
cd /mnt/d/OS\ experiment/course-design-b/.local
mkdir -p debs sysroot
cd debs
apt-get download cmake cmake-data libclang1-18 libllvm18 libjsoncpp25 librhash0 libarchive13t64 libuv1t64
for f in *.deb; do dpkg-deb -x "$f" ../sysroot; done