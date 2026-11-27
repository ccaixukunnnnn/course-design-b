#!/bin/bash
set -euo pipefail
cd /mnt/d/OS\ experiment/course-design-b
expected=2e075accf4fb0aefdd1d252ebd9ccf29727d9923
test "$(git rev-parse HEAD)" = "$expected" || { echo 'unexpected HEAD' >&2; exit 1; }
test ! -e .local/baseline || { echo 'baseline already exists; refusing to overwrite' >&2; exit 1; }
mkdir -p .local/baseline
git archive "$expected" | tar -x -C .local/baseline
