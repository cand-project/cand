#!/usr/bin/env bash
set -euo pipefail
cand="${1:?cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
for source in tests/storage/*.c; do
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$work/cand.json" 2>"$work/cand.err"
  rc=$?
  set -e
  result="$(python3 - "$work/cand.json" <<'PYJSON'
import json, sys
try:
    print(json.load(open(sys.argv[1], encoding='utf-8')).get('result', ''))
except Exception:
    print('')
PYJSON
)"
  base="$(basename "$source")"
  case "$base" in
    *incomplete*) [[ "$rc" == 3 && "$result" == incomplete ]] || { echo "expected incomplete: $source"; cat "$work/cand.json"; cat "$work/cand.err"; exit 1; } ;;
    *uaf*|*double_free*) [[ "$rc" == 1 && "$result" == fail ]] || { echo "expected fail: $source"; cat "$work/cand.json"; cat "$work/cand.err"; exit 1; } ;;
    *) [[ "$rc" == 0 && "$result" == pass ]] || { echo "expected pass: $source"; cat "$work/cand.json"; cat "$work/cand.err"; exit 1; } ;;
  esac

  if ! clang -std=c11 -g -fsanitize=address "$source" -o "$work/a.out" >"$work/clang.out" 2>&1; then
    echo "fixture failed to compile with ASan: $source"; cat "$work/clang.out"; exit 1
  fi
  set +e
  "$work/a.out" >"$work/asan" 2>&1
  asan_rc=$?
  set -e
  if [[ "$base" == *uaf* || "$base" == *double_free* ]]; then
    grep -q "ERROR: AddressSanitizer" "$work/asan" || { echo "ASan did not confirm expected violation: $source (rc=$asan_rc)"; cat "$work/asan"; exit 1; }
  elif [[ "$base" != *incomplete* ]]; then
    if grep -q "ERROR: AddressSanitizer" "$work/asan" || [[ "$asan_rc" != 0 ]]; then
      echo "safe fixture failed under ASan: $source"; cat "$work/asan"; exit 1
    fi
  fi
done
echo "C& P0.3 storage tests passed."
