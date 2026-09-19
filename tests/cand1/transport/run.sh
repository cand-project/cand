#!/usr/bin/env bash
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root"

for source in tests/p2/aggregate_transport.c tests/p2/array_transport.c \
    tests/p2/memcpy_transport.c tests/p2/cast_transport.c \
    tests/p2/union_transport.c tests/p2/realloc_borrow.c \
    tests/cand1/transport/annotation_removal.c; do
    set +e
    output="$($cand check --level=cand1 --format=json "$source" -- -std=c11 -Iinclude 2>/dev/null)"
    rc=$?
    set -e
    python3 - "$source" "$rc" "$output" <<'PY'
import json
import sys
source, rc, text = sys.argv[1:]
report = json.loads(text)
if report.get("result") == "pass":
    raise SystemExit(f"tracked transport passed: {source}: {report}")
if not report.get("findings") and not report.get("unsupported"):
    raise SystemExit(f"tracked transport metadata disappeared: {source}: {report}")
PY
done

set +e
annotation_output="$($cand check --level=cand1 --format=json tests/cand1/transport/annotation_removal.c -- -std=c11 -Iinclude 2>/dev/null)"
set -e
python3 - "$annotation_output" <<'PY'
import json
import sys
report = json.loads(sys.argv[1])
if report.get("result") == "pass":
    raise SystemExit("annotation removal weakened cand1 proof scope to PASS")
PY

for source in tests/p2/aggregate_transport.c tests/p2/array_transport.c \
    tests/p2/memcpy_transport.c tests/p2/cast_transport.c \
    tests/p2/union_transport.c tests/p2/realloc_borrow.c; do
    cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
    clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
done

python3 tests/cand1/transport/adversarial.py "$cand"
python3 tests/cand1/transport/benchmark.py "$cand"
echo "C&1-B transport boundary corpus passed."
