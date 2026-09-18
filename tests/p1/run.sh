#!/usr/bin/env bash
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

check_result() {
    local expected="$1" expected_id="$2" source="$3"
    local output rc
    set +e
    output="$($cand check --format=json "$source" -- -std=c11 -Iinclude 2>/dev/null)"
    rc=$?
    set -e
    python3 - "$expected" "$expected_id" "$rc" "$output" <<'PY'
import json
import sys

expected, expected_id, rc, text = sys.argv[1:]
data = json.loads(text)
if data.get("result") != expected:
    raise SystemExit(f"{sys.argv[3]}: expected {expected}, got {data.get('result')}: {data}")
if expected == "fail" and expected_id not in [item.get("id") for item in data.get("findings", [])]:
    raise SystemExit(f"missing {expected_id}: {data.get('findings')}")
if expected == "pass" and (data.get("findings") or data.get("unsupported")):
    raise SystemExit(f"unexpected diagnostics: {data}")
if expected == "fail" and int(rc) != 1:
    raise SystemExit(f"semantic failure returned {rc}")
if expected == "incomplete" and int(rc) != 3:
    raise SystemExit(f"incomplete result returned {rc}")
PY
}

check_result fail CAND-O001 tests/p1/move_use_after_move.c
check_result fail CAND-O002 tests/p1/double_move.c
check_result fail CAND-O003 tests/p1/stale_owner_destroy.c
check_result fail CAND-O001 tests/p1/conditional_move.c
check_result fail CAND-O004 tests/p1/self_move.c
set +e
self_move_output="$($cand check --format=json tests/p1/self_move.c -- -std=c11 -Iinclude 2>/dev/null)"
self_move_rc=$?
set -e
python3 - "$self_move_rc" "$self_move_output" <<'PY'
import json
import sys
data = json.loads(sys.argv[2])
if int(sys.argv[1]) != 1:
    raise SystemExit(f"self-move returned {sys.argv[1]}")
finding = next((item for item in data.get("findings", []) if item.get("id") == "CAND-O004"), None)
if finding is None:
    raise SystemExit(f"missing self-move finding: {data}")
primary = finding.get("primary_location", {})
move = finding.get("move_location", {})
if move.get("line") != primary.get("line"):
    raise SystemExit(f"self-move location drift: {finding}")
PY
check_result pass "" tests/p1/move_safe.c
check_result pass "" tests/p1/owned_return_after_move.c
check_result incomplete "" tests/p1/move_named_identifier.c
check_result incomplete "" tests/p1/owner_overwrite_incomplete.c
check_result incomplete "" tests/p1/standalone_move_incomplete.c
check_result incomplete "" tests/p1/missing_explicit_transfer_semantic.c
check_result incomplete "" tests/p1/aggregate_move_incomplete.c
check_result incomplete "" tests/p1/move_to_borrow_incomplete.c
check_result fail CAND-O001 tests/p1/macro_move_use_after_move.c

set +e
generated_output="$($cand check --profile=generated --format=json tests/p1/missing_explicit_transfer.c -- -std=c11 -Iinclude 2>/dev/null)"
generated_rc=$?
set -e
python3 - "$generated_rc" "$generated_output" <<'PY'
import json
import sys
data = json.loads(sys.argv[2])
if int(sys.argv[1]) != 4 or data.get("semantic_result") != "fail":
    raise SystemExit(f"generated strict transfer check failed: {data}")
findings = data.get("analysis", {}).get("findings", [])
if not any(item.get("id") == "CAND-O005" for item in findings):
    raise SystemExit(f"missing CAND-O005: {findings}")
PY

set +e
contract_output="$($cand check --contracts=tests/p1/trusted_consumer.yaml --format=json tests/p1/trusted_consumer_move.c -- -std=c11 -Iinclude 2>/dev/null)"
contract_rc=$?
set -e
python3 - "$contract_rc" "$contract_output" <<'PY'
import json
import sys
if int(sys.argv[1]) != 0:
    raise SystemExit(f"trusted consumer contract failed: {sys.argv[2]}")
data = json.loads(sys.argv[2])
if data.get("result") != "pass":
    raise SystemExit(f"trusted consumer move did not pass: {data}")
PY

for source in tests/p1/*.c; do
    cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
    clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
done

echo "C& P1 explicit ownership tests passed."
