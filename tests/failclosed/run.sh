#!/usr/bin/env bash
#
# P0.1 fail-closed regression tests.
#
# Central invariant under test:
#
#   False INCOMPLETE is temporarily acceptable. False PASS is not.
#
# Every fixture here encodes a required cand result. Unsupported ownership
# must remain incomplete; P0.3 promotes local members/elements to findings.
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

expect_incomplete() {
  local source="$1"
  local expected_kind="$2"
  local tmp
  tmp="$(mktemp)"
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$tmp"
  local rc=$?
  set -e
  if [[ "$rc" -ne 3 ]]; then
    echo "expected cand exit 3 (incomplete) for $source, got $rc" >&2
    cat "$tmp" >&2 || true
    rm -f "$tmp"
    exit 1
  fi
  python3 - "$tmp" "$expected_kind" <<'PY'
import json
import sys

path, expected_kind = sys.argv[1], sys.argv[2]
with open(path, "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") != "incomplete":
    raise SystemExit(f"expected incomplete, got {data.get('result')!r}: {data}")
kinds = [item.get("kind") for item in data.get("unsupported", [])]
if not any(kind == expected_kind or str(kind).startswith(expected_kind + ":")
           for kind in kinds):
    raise SystemExit(f"expected unsupported kind {expected_kind!r}, got {kinds!r}")
coverage = data.get("coverage", {})
if "tracked_heap_objects" not in coverage:
    raise SystemExit("coverage summary is missing tracked_heap_objects")
if "unsupported_ownership_operations" not in coverage:
    raise SystemExit("coverage summary is missing unsupported_ownership_operations")
PY
  rm -f "$tmp"
}


expect_fail() {
  local source="$1"
  local expected_id="$2"
  local tmp
  tmp="$(mktemp)"
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$tmp"
  local rc=$?
  set -e
  if [[ "$rc" -ne 1 ]]; then
    echo "expected cand exit 1 (fail) for $source, got $rc" >&2
    cat "$tmp" >&2 || true
    rm -f "$tmp"
    exit 1
  fi
  python3 - "$tmp" "$expected_id" <<'PY'
import json
import sys

path, expected = sys.argv[1], sys.argv[2]
with open(path, "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") != "fail":
    raise SystemExit(f"expected fail, got {data.get('result')!r}: {data}")
ids = [item.get("id") for item in data.get("findings", [])]
if expected not in ids:
    raise SystemExit(f"expected {expected}, got {ids}")
for item in data.get("findings", []):
    if item.get("id") == expected:
        if item.get("certainty") not in ("definite", "possible"):
            raise SystemExit(f"finding missing certainty: {item}")
        if not item.get("state_trace"):
            raise SystemExit(f"finding missing state trace: {item}")
PY
  rm -f "$tmp"
}

expect_pass() {
  local source="$1"
  local tmp
  tmp="$(mktemp)"
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$tmp"
  python3 - "$tmp" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") != "pass":
    raise SystemExit(f"expected pass, got {data.get('result')!r}: {data}")
if data.get("findings"):
    raise SystemExit(f"expected no findings: {data['findings']}")
if data.get("unsupported"):
    raise SystemExit(f"expected no unsupported constructs: {data['unsupported']}")
PY
  rm -f "$tmp"
}

# --- BLOCKER regressions: must be INCOMPLETE, never PASS ---------------------

expect_fail tests/failclosed/struct_member_uaf.c CAND-T002
expect_fail tests/failclosed/array_element_uaf.c CAND-T002
expect_fail tests/failclosed/wrapper_return_uaf.c CAND-T002

# --- Fail-closed semantics ----------------------------------------------------

expect_pass tests/failclosed/pointer_param_free.c
expect_incomplete tests/failclosed/stack_pointer_return.c stack-pointer-return
expect_incomplete tests/failclosed/stack_pointer_variable_return.c stack-pointer-return
expect_incomplete tests/failclosed/aggregate_initializer.c \
    allocation-to-untracked-storage
expect_incomplete tests/failclosed/aggregate_initializer_unknown.c \
    allocation-to-untracked-storage
expect_incomplete tests/failclosed/statement_expression.c statement-expression
expect_incomplete tests/failclosed/inline_asm.c inline-asm
expect_pass tests/p0/alias_unsupported.c
expect_pass tests/p0/unknown_call_unsupported.c

# --- Known-safe cases that must stay PASS -------------------------------------

# P0.2: conditions and short-circuit operators are modeled over the CFG.
expect_pass tests/failclosed/conditional_ownership.c
expect_fail tests/failclosed/short_circuit_ownership.c CAND-T003

expect_pass tests/failclosed/free_null_safe.c
expect_pass tests/failclosed/conditional_known_safe.c
expect_pass tests/failclosed/static_pointer_return_known_safe.c

echo "C& P0.1 fail-closed tests passed."
