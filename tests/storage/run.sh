#!/usr/bin/env bash
set -euo pipefail
cand="${1:?cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
for source in tests/storage/*.c; do
  set +e
  "$cand" check --format=json "$source" -- -std=c11 -Iinclude >"$work/cand.json" 2>/dev/null
  rc=$?
  clang -std=c11 -g -fsanitize=address "$source" -o "$work/a.out" >/dev/null 2>&1
  "$work/a.out" >"$work/asan" 2>&1
  asan=$?
  set -e
  result="$(sed -n 's/.*"result": "\([^"]*\)".*/\1/p' "$work/cand.json" | head -1)"
  case "$(basename "$source")" in
    *dynamic_index*) [[ "$rc" == 3 && "$result" == incomplete ]] || { echo "expected incomplete: $source"; exit 1; } ;;
    *uaf*|*double_free*) [[ "$rc" == 1 && "$result" == fail ]] || { echo "expected fail: $source"; exit 1; } ;;
    *) [[ "$rc" == 0 && "$result" == pass ]] || { echo "expected pass: $source"; exit 1; } ;;
  esac
  if [[ "$source" == *uaf* || "$source" == *double_free* ]]; then
    [[ "$asan" != 0 ]] || { echo "ASan missed expected violation: $source"; exit 1; }
  elif [[ "$source" != *dynamic_index* && "$asan" != 0 ]]; then
    echo "ordinary safe fixture crashed: $source"; exit 1
  fi
done
echo "C& P0.3 storage tests passed."
