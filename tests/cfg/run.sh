#!/usr/bin/env bash
#
# P0.2 CFG / flow-sensitive corpus.
#
# Each fixture declares the cand result that flow-sensitive analysis must
# produce. Where the fixture is executable, the same result is cross-checked
# against AddressSanitizer as an independent dynamic oracle:
#
#   expected PASS -> ASan must be clean
#   expected FAIL -> ASan must report a runtime violation
#
# Any fixture with an ASan-confirmed violation that cand passes is a BLOCKER.
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cand_json() {
  local source="$1"
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$work/cand.json"
  local rc=$?
  set -e
  if [[ "$rc" -ne 0 && "$rc" -ne 1 && "$rc" -ne 3 ]]; then
    echo "cand returned unexpected exit $rc for $source" >&2
    cat "$work/cand.json" >&2 || true
    exit 1
  fi
  echo "$rc"
}

ran_asan=0
asan_result() {
  local source="$1"
  if ! clang -std=c11 -g -fsanitize=address "$source" -o "$work/asan" 2>"$work/clang.log"; then
    echo "compile-failed"
    return
  fi
  set +e
  "$work/asan" >"$work/asan.out" 2>&1
  set -e
  ran_asan=1
  if grep -q "ERROR: AddressSanitizer" "$work/asan.out"; then
    echo "violation"
  else
    echo "clean"
  fi
}

check_pass() {
  local source="$1"
  local rc
  rc="$(cand_json "$source")"
  if [[ "$rc" -ne 0 ]]; then
    echo "expected PASS for $source, got exit $rc" >&2
    cat "$work/cand.json" >&2
    exit 1
  fi
  python3 - "$work/cand.json" "$source" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") != "pass":
    raise SystemExit(f"{sys.argv[2]}: expected pass, got {data.get('result')!r}")
if data.get("findings") or data.get("unsupported"):
    raise SystemExit(f"{sys.argv[2]}: expected clean pass, got {data}")
PY
  local asan
  asan="$(asan_result "$source")"
  if [[ "$asan" == "violation" ]]; then
    echo "BLOCKER: $source expected PASS but ASan reports a violation" >&2
    exit 1
  fi
  echo "cfg OK (pass, asan=$asan): $source"
}

check_fail() {
  local source="$1"
  local expected_id="$2"
  local rc
  rc="$(cand_json "$source")"
  if [[ "$rc" -ne 1 ]]; then
    echo "expected FAIL for $source, got exit $rc" >&2
    cat "$work/cand.json" >&2
    exit 1
  fi
  python3 - "$work/cand.json" "$source" "$expected_id" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") != "fail":
    raise SystemExit(f"{sys.argv[2]}: expected fail, got {data.get('result')!r}")
ids = [item.get("id") for item in data.get("findings", [])]
if sys.argv[3] not in ids:
    raise SystemExit(f"{sys.argv[2]}: expected {sys.argv[3]}, got {ids}")
for item in data.get("findings", []):
    if item.get("id") == sys.argv[3]:
        if item.get("certainty") not in ("definite", "possible"):
            raise SystemExit(f"{sys.argv[2]}: finding lacks certainty: {item}")
        if not item.get("state_trace"):
            raise SystemExit(f"{sys.argv[2]}: finding lacks state trace: {item}")
PY
  local asan
  asan="$(asan_result "$source")"
  if [[ "$asan" != "violation" ]]; then
    echo "expected ASan violation for negative fixture $source (asan=$asan)" >&2
    exit 1
  fi
  echo "cfg OK (fail $expected_id, asan=violation): $source"
}

check_pass tests/cfg/early_return_safe.c
check_pass tests/cfg/both_branches_free_safe.c
check_pass tests/cfg/branch_free_return_safe.c
check_pass tests/cfg/nested_if_safe.c
check_pass tests/cfg/loop_no_ownership_change.c
check_pass tests/cfg/null_release_safe.c
check_pass tests/cfg/ternary_allocation_safe.c
check_pass tests/cfg/short_circuit_guard_safe.c

check_fail tests/cfg/conditional_free_uaf.c CAND-T002
check_fail tests/cfg/conditional_double_free.c CAND-T003
check_fail tests/cfg/both_branches_free_then_use.c CAND-T002
check_fail tests/cfg/nested_if_uaf.c CAND-T002
check_fail tests/cfg/loop_possible_double_free.c CAND-T003
check_fail tests/cfg/branch_null_or_free_possible_uaf.c CAND-T002
check_fail tests/cfg/branch_free_or_null_double_free.c CAND-T003

echo "C& P0.2 CFG corpus passed."
