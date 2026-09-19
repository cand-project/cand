#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for name in first second; do
    cmake -S "$repo" -B "$work/$name" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-18 >/dev/null
    cmake --build "$work/$name" --parallel 2 >/dev/null
    sha256sum "$work/$name/cand" | cut -d' ' -f1 > "$work/$name.sha256"
done

cmp -s "$work/first.sha256" "$work/second.sha256"
echo "reproducible verifier binary: PASS ($(cat "$work/first.sha256"))"
