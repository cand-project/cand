#!/usr/bin/env bash
set -euo pipefail

cand_bin="$(realpath "$1")"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manifest="$repo/toolchains/cand1-v1-linux-x86_64.json"
clang_bin="/usr/bin/clang-18"
gcc_bin="/usr/bin/gcc-13"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if cmake -S "$repo" -B "$work/launcher" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_CXX_COMPILER_LAUNCHER=/bin/true >/dev/null 2>&1; then
    echo "compiler launcher injection was accepted" >&2
    exit 1
fi
if CLANG_CONFIG_FILE=/tmp/cand1-invalid-clang.cfg cmake -S "$repo" -B "$work/config" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-18 >/dev/null 2>&1; then
    echo "Clang config-file injection was accepted" >&2
    exit 1
fi

for variable in CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH \
    COMPILER_PATH GCC_EXEC_PREFIX LIBRARY_PATH LD_LIBRARY_PATH LD_PRELOAD \
    CFLAGS CPPFLAGS CXXFLAGS LDFLAGS CLANG_CONFIG_FILE; do
    if [[ -n "${!variable:-}" ]]; then
        echo "unsupported toolchain environment: $variable" >&2
        exit 1
    fi
done
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH COMPILER_PATH \
    GCC_EXEC_PREFIX LIBRARY_PATH LD_LIBRARY_PATH LD_PRELOAD CFLAGS CPPFLAGS \
    CXXFLAGS LDFLAGS CLANG_CONFIG_FILE
export PATH=/usr/bin:/bin
[[ -x "$cand_bin" ]] || { echo "verifier binary is not executable" >&2; exit 1; }
set +e
"$cand_bin" >/dev/null 2>&1
cand_rc=$?
set -e
[[ "$cand_rc" == 2 ]] || { echo "verifier binary is not runnable" >&2; exit 1; }

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
[[ "$(realpath "$clang_bin")" == "/usr/lib/llvm-18/bin/clang" ]] || { echo "unexpected Clang path" >&2; exit 1; }
[[ "$(realpath "$gcc_bin")" == "/usr/bin/x86_64-linux-gnu-gcc-13" ]] || { echo "unexpected GCC path" >&2; exit 1; }

for compiler in "$clang_bin" "$gcc_bin"; do
    "$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror -I"$repo/include" \
        "$repo/tests/toolchain/compatibility.c" -o "$work/compatibility"
    [[ "$($work/compatibility)" == "" ]]
    "$compiler" -std=c11 -pedantic-errors -I"$repo/include" -c \
        "$repo/tests/toolchain/compatibility.c" -o "$work/plain.o"
    "$compiler" -std=c11 -pedantic-errors -DCAND_ANALYSIS=1 -I"$repo/include" -c \
        "$repo/tests/toolchain/compatibility.c" -o "$work/annotated.o"
    nm -g --defined-only "$work/plain.o" | sort > "$work/plain.symbols"
    nm -g --defined-only "$work/annotated.o" | sort > "$work/annotated.symbols"
    cmp -s "$work/plain.symbols" "$work/annotated.symbols"
done

echo "toolchain compatibility: PASS"
