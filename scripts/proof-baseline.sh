#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

unsafe_uaf="tests/proof/temporal/use_after_free_unsafe.c"
fixed_uaf="tests/proof/temporal/use_after_free_fixed.c"
unsafe_df="tests/proof/temporal/double_free_unsafe.c"
fixed_df="tests/proof/temporal/double_free_fixed.c"

echo "==> Ordinary compilers accept the temporal-defect fixtures"
for src in "$unsafe_uaf" "$unsafe_df"; do
  gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only "$src"
  clang -std=c11 -Wall -Wextra -Werror -fsyntax-only "$src"
done

echo "==> Independent runtime oracle observes the defects"
clang -std=c11 -O0 -g -fsanitize=address -fno-omit-frame-pointer "$unsafe_uaf" -o "$tmp/uaf"
set +e
ASAN_OPTIONS=detect_leaks=1 "$tmp/uaf" >"$tmp/uaf.out" 2>"$tmp/uaf.err"
uaf_rc=$?
set -e
test "$uaf_rc" -ne 0
grep -Fq "heap-use-after-free" "$tmp/uaf.err"

clang -std=c11 -O0 -g -fsanitize=address -fno-omit-frame-pointer "$unsafe_df" -o "$tmp/df"
set +e
ASAN_OPTIONS=detect_leaks=1 "$tmp/df" >"$tmp/df.out" 2>"$tmp/df.err"
df_rc=$?
set -e
test "$df_rc" -ne 0
grep -Fq "double-free" "$tmp/df.err"

echo "==> Corrected fixtures are clean under the same runtime oracle"
for src in "$fixed_uaf" "$fixed_df"; do
  bin="$tmp/$(basename "$src" .c)"
  gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only "$src"
  clang -std=c11 -Wall -Wextra -Werror -fsyntax-only "$src"
  clang -std=c11 -O0 -g -fsanitize=address -fno-omit-frame-pointer "$src" -o "$bin"
  ASAN_OPTIONS=detect_leaks=1 "$bin"
done

echo "Differential ordinary-C proof baseline passed."
