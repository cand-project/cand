#!/usr/bin/env bash
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

check_failure_json() {
  local expected_id="$1"
  local json_file="$2"
  python3 - "$expected_id" "$json_file" <<'PY'
import json
import sys

expected = sys.argv[1]
path = sys.argv[2]
with open(path, "r", encoding="utf-8") as handle:
    data = json.load(handle)

if data.get("schema") != "cand.check/v1":
    raise SystemExit(f"unexpected schema: {data.get('schema')!r}")
if data.get("result") != "fail":
    raise SystemExit(f"expected fail, got: {data.get('result')!r}")
ids = [item.get("id") for item in data.get("findings", [])]
if expected not in ids:
    raise SystemExit(f"expected {expected}, got findings: {ids}")
for item in data.get("findings", []):
    if item.get("id") == expected:
        if not item.get("object_id", "").startswith("obj:"):
            raise SystemExit("finding is missing stable object id")
        if len(item.get("state_trace", [])) < 3:
            raise SystemExit("finding is missing state trace")
PY
}

expect_failure() {
  local expected_id="$1"
  local source="$2"
  local tmp
  tmp="$(mktemp)"
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$tmp"
  local rc=$?
  set -e
  if [[ "$rc" -ne 1 ]]; then
    echo "expected cand exit 1 for $source, got $rc" >&2
    cat "$tmp" >&2 || true
    rm -f "$tmp"
    exit 1
  fi
  check_failure_json "$expected_id" "$tmp"
  rm -f "$tmp"
}

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
if not any(kind == expected_kind or str(kind).startswith(expected_kind + ":") for kind in kinds):
    raise SystemExit(f"expected unsupported kind {expected_kind!r}, got {kinds!r}")
if data.get("findings"):
    raise SystemExit(f"expected no temporal finding in unsupported-only fixture: {data['findings']}")
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

expect_failure CAND-T002 tests/proof/temporal/use_after_free_unsafe.c
expect_failure CAND-T002 tests/proof/temporal/use_after_free_annotated_unsafe.c
expect_failure CAND-T003 tests/proof/temporal/double_free_unsafe.c
expect_pass tests/p0/alias_unsupported.c
expect_incomplete tests/p0/unknown_call_unsupported.c unknown-call-with-tracked-pointer
expect_pass tests/proof/temporal/use_after_free_fixed.c
expect_pass tests/proof/temporal/double_free_fixed.c

echo "C& P0 semantic-core tests passed."
