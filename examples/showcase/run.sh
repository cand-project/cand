#!/usr/bin/env bash
set -euo pipefail

cand="${1:?usage: run.sh /path/to/cand}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

run_case() {
  local label="$1"
  local source="$2"
  local expected_result="$3"
  local expected_rc="$4"
  local expected_token="$5"
  local tmp
  tmp="$(mktemp)"

  set +e
  "$cand" check --format=json "$source" -- -std=c11 >"$tmp"
  local rc=$?
  set -e

  python3 - "$label" "$expected_result" "$expected_rc" "$expected_token" "$rc" "$tmp" <<'PY'
import json
import sys

label, expected_result, expected_rc, expected_token, actual_rc, path = sys.argv[1:]
actual_rc = int(actual_rc)
expected_rc = int(expected_rc)

with open(path, "r", encoding="utf-8") as handle:
    data = json.load(handle)

if actual_rc != expected_rc:
    raise SystemExit(f"{label}: expected exit {expected_rc}, got {actual_rc}: {data}")
if data.get("result") != expected_result:
    raise SystemExit(f"{label}: expected {expected_result}, got {data.get('result')}: {data}")

if expected_result == "fail":
    ids = [item.get("id") for item in data.get("findings", [])]
    if expected_token not in ids:
        raise SystemExit(f"{label}: expected finding {expected_token}, got {ids}")
elif expected_result == "incomplete":
    kinds = [item.get("kind", "") for item in data.get("unsupported", [])]
    if not any(expected_token in kind for kind in kinds):
        raise SystemExit(f"{label}: expected unsupported token {expected_token!r}, got {kinds}")

print(f"{label:<28} -> {expected_result.upper():<10} {expected_token if expected_token != '-' else ''}")
PY

  rm -f "$tmp"
}

run_case "Asteroid Arena unsafe" \
  examples/showcase/01-asteroid-arena-unsafe.c fail 1 CAND-T002
run_case "Asteroid Arena fixed" \
  examples/showcase/01-asteroid-arena-fixed.c pass 0 -
run_case "Dungeon Loot unsafe" \
  examples/showcase/02-dungeon-loot-unsafe.c fail 1 CAND-T003
run_case "Dungeon Loot fixed" \
  examples/showcase/02-dungeon-loot-fixed.c pass 0 -
run_case "Snake Tail unsafe" \
  examples/showcase/03-snake-tail-unsafe.c fail 1 CAND-T002
run_case "Snake Tail fixed" \
  examples/showcase/03-snake-tail-fixed.c pass 0 -
run_case "RPG Inventory Alias" \
  examples/showcase/04-rpg-inventory-alias-incomplete.c incomplete 3 pointer-alias-initialization
run_case "Renderer Plugin Boundary" \
  examples/showcase/05-renderer-plugin-boundary-incomplete.c incomplete 3 unknown-call-with-tracked-pointer

echo
echo "C& showcase passed: concrete bugs fail, repairs pass, unknown semantics fail closed."
