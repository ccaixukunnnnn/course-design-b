#!/bin/bash
set -e
B=$(cd "$(dirname "$0")/../.." && pwd -P)
T=$(mktemp -d /tmp/starry-course-b-cache.XXXXXXXX)
mkdir -p "$T/cargo/registry"
cp -a "$B/.local/cargo/registry/cache" "$B/.local/cargo/registry/index" "$T/cargo/registry/"
printf '%s\n' "$T" > "$B/.local/cache-tmp-path.txt"
