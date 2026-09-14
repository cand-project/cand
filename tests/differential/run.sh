#!/usr/bin/env bash
#
# Differential evidence runner (P0.1).
#
# For every negative fixture this proves the three-way differential:
#
#   ordinary compiler (gcc)  -> accepts the program
#   ASan (clang)             -> demonstrates the runtime heap violation
#   cand                     -> FAIL or INCOMPLETE (never PASS)
#
# A negative fixture where ASan reports a violation but cand returns PASS
# fails this script, and therefore CI. ASan is an independent dynamic oracle
# for catching analyzer regressions; it is not a formal proof engine.
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

negative_fixture() {
  local source="$1"

  # 1. Ordinary compiler accepts the program.
  gcc -std=c11 -Wall -Wextra "$source" -o "$work/plain" 2>"$work/gcc.log"

  # 2. ASan demonstrates the runtime violation.
  if ! clang -std=c11 -g -fsanitize=address "$source" -o "$work/asan" 2>"$work/clang.log"; then
    echo "ASan build failed for negative fixture $source" >&2
    cat "$work/clang.log" >&2
    exit 1
  fi
  set +e
  "$work/asan" >"$work/asan.out" 2>&1
  local asan_rc=$?
  set -e
  if [[ "$asan_rc" -eq 0 ]]; then
    echo "negative fixture $source did not trigger a sanitizer violation (rc=0)" >&2
    cat "$work/asan.out" >&2
    exit 1
  fi
  if ! grep -q "AddressSanitizer" "$work/asan.out"; then
    echo "negative fixture $source failed without an AddressSanitizer report" >&2
    cat "$work/asan.out" >&2
    exit 1
  fi

  # 3. cand must refuse PASS.
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$work/cand.json"
  local cand_rc=$?
  set -e
  if [[ "$cand_rc" -eq 0 ]]; then
    echo "BLOCKER: $source has an ASan violation but cand returned PASS" >&2
    exit 1
  fi
  if [[ "$cand_rc" -ne 1 && "$cand_rc" -ne 3 ]]; then
    echo "cand returned unexpected exit $cand_rc for $source" >&2
    cat "$work/cand.json" >&2 || true
    exit 1
  fi
  python3 - "$work/cand.json" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") == "pass":
    raise SystemExit("cand JSON result is 'pass' for an ASan-confirmed violation")
PY

  echo "differential OK (ASan violation, cand: $cand_rc): $source"
}

positive_fixture() {
  local source="$1"

  clang -std=c11 -g -fsanitize=address "$source" -o "$work/asan" 2>"$work/clang.log"
  "$work/asan" >"$work/asan.out" 2>&1

  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$work/cand.json"
  python3 - "$work/cand.json" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("result") != "pass":
    raise SystemExit(f"expected cand pass for safe fixture, got {data.get('result')!r}")
PY

  echo "differential OK (ASan clean, cand: pass): $source"
}

negative_fixture tests/differential/direct_uaf.c
negative_fixture tests/differential/double_free.c
negative_fixture tests/differential/struct_member_uaf.c
negative_fixture tests/differential/array_element_uaf.c
negative_fixture tests/differential/wrapper_return_uaf.c
positive_fixture tests/differential/safe_lifecycle.c

echo "C& differential ASan evidence passed."
