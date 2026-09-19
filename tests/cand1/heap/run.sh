#!/usr/bin/env bash
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root"

check_result() {
    local expected="$1" expected_id="$2" source="$3"
    local output rc
    set +e
    output="$($cand check --level=cand1 --format=json "$source" -- -std=c11 -Iinclude 2>/dev/null)"
    rc=$?
    set -e
    python3 - "$expected" "$expected_id" "$source" "$rc" "$output" <<'PY'
import json
import sys
expected, expected_id, source, rc, text = sys.argv[1:]
report = json.loads(text)
actual = report.get("result")
if actual != expected:
    raise SystemExit(f"{source}: expected {expected}, got {actual}: {report}")
if expected == "incomplete" and not expected_id and report.get("findings"):
    raise SystemExit(f"{source}: safe heap case produced semantic findings: {report['findings']}")
if expected_id and expected_id not in [x.get("id") for x in report.get("findings", [])]:
    raise SystemExit(f"{source}: missing {expected_id}: {report}")
if expected == "incomplete" and int(rc) != 3:
    raise SystemExit(f"{source}: expected exit 3, got {rc}")
if expected == "fail" and int(rc) != 1:
    raise SystemExit(f"{source}: expected exit 1, got {rc}")
PY
    echo "C&1 heap OK ($expected): $source"
}

check_result incomplete "" tests/cand1/heap/safe_per_iteration.c
check_result fail CAND-T002 tests/cand1/heap/stale_alias.c
check_result incomplete "" tests/cand1/heap/nested_safe.c
check_result incomplete "" tests/cand1/heap/conditional.c
check_result incomplete "" tests/cand1/heap/repeated_call.c
check_result fail CAND-T002 tests/cand1/heap/nested_stale_alias.c
check_result fail CAND-T002 tests/cand1/heap/conditional_stale_alias.c

set +e
first="$($cand check --level=cand1 --format=json tests/cand1/heap/stale_alias.c -- -std=c11 -Iinclude 2>/dev/null)"
first_rc=$?
second="$($cand check --level=cand1 --format=json tests/cand1/heap/stale_alias.c -- -std=c11 -Iinclude 2>/dev/null)"
second_rc=$?
set -e
python3 - "$first" "$second" "$first_rc" "$second_rc" <<'PY'
import sys
if sys.argv[1] != sys.argv[2] or sys.argv[3] != sys.argv[4]:
    raise SystemExit("heap diagnostics are not deterministic")
print("C&1 heap diagnostics deterministic")
PY

for source in tests/cand1/heap/*.c; do
    cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
    clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
done

python3 tests/cand1/heap/benchmark.py "$cand"

echo "C&1-A heap fixtures passed (experimental strict mode remains evidence-gated)."
