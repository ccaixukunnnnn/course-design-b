#!/bin/bash
set -euo pipefail
cd /mnt/d/OS\ experiment/course-design-b
expected=2e075accf4fb0aefdd1d252ebd9ccf29727d9923
git cat-file -e "$expected^{commit}" || { echo 'missing upstream baseline commit' >&2; exit 1; }
dir=.local/perf-baseline
test ! -e "$dir" || { echo 'baseline source already exists; refusing to overwrite' >&2; exit 1; }
mkdir -p "$dir"
git archive "$expected" | tar -x -C "$dir"
cp coursework/baseline-eventfd/event.rs "$dir/kernel/src/file/event.rs"
cp coursework/baseline-eventfd/event_counter.rs "$dir/kernel/src/file/event_counter.rs"
python3 - "$dir/kernel/src/file/mod.rs" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
source = path.read_text()
needle = 'pub mod event;\n'
if source.count(needle) != 1:
    raise SystemExit('unexpected upstream file layout')
path.write_text(source.replace(needle, needle + 'mod event_counter;\n'))
PY
