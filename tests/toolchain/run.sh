#!/usr/bin/env bash
set -euo pipefail

cand_bin="$(realpath "$1")"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manifest="$repo/toolchains/cand1-v1-linux-x86_64.json"
clang_bin="${CAND_CLANG:-clang}"
gcc_bin="${CAND_GCC:-gcc}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

readarray -t expected < <(python3 - "$manifest" <<'PY'
import json, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
print(m["production_compilers"]["clang"])
print(m["production_compilers"]["gcc"])
PY
)
clang_version="$($clang_bin --version | sed -n '1s/.*clang version \([0-9.]*\).*/\1/p;1s/.*clang-[^ ]* \([0-9.]*\).*/\1/p')"
gcc_version="$($gcc_bin -dumpfullversion -dumpversion)"
[[ "$clang_version" == "${expected[0]}" ]] || { echo "unsupported Clang: $clang_version" >&2; exit 1; }
[[ "$gcc_version" == "${expected[1]}" ]] || { echo "unsupported GCC: $gcc_version" >&2; exit 1; }

for compiler in "$clang_bin" "$gcc_bin"; do
    "$compiler" -std=c11 -Wall -Wextra -Werror -I"$repo/include" \
        "$repo/tests/toolchain/compatibility.c" -o "$work/compatibility"
    [[ "$($work/compatibility)" == "" ]]
done

echo "toolchain compatibility: PASS"
