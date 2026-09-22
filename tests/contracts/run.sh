#!/usr/bin/env bash
set -euo pipefail

# Conformance and soundness harness for the reviewed external-API contract
# bundles (milestone #58). Layers:
#
#   1. static bundle policy (scripts/contracts/check_bundles.py):
#      completeness, scalar-only no_ownership_effect, excluded symbols,
#      provenance sections;
#   2. merge tool invariants (scripts/contracts/merge_contracts.py):
#      determinism, duplicate-symbol rejection, malformed-bundle rejection,
#      no committed merge product;
#   3. executable conformance fixtures: positive (bundles clear the
#      boundary obligations and the file PASSES) and adversarial (a wrong
#      contract would create a false PASS; each fixture pins the sound
#      verdict that must survive).
#
# Usage: tests/contracts/run.sh <path-to-cand>

cand="$1"
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

merged="$work/merged-contracts.yaml"

echo "==> contracts: static bundle policy"
python3 scripts/contracts/check_bundles.py

echo "==> contracts: deterministic merge"
python3 scripts/contracts/merge_contracts.py --out "$merged" --digests
python3 scripts/contracts/merge_contracts.py --out "$work/merged-again.yaml" >/dev/null
cmp "$merged" "$work/merged-again.yaml" || {
  echo "merge is not deterministic"; exit 1;
}

echo "==> contracts: no committed merge product"
# The merged contract file is a build product and must never be committed:
# a stale or candidate-modified merged file could silently replace the
# reviewed bundle set. Only reviewed sources may be tracked.
committed="$(git ls-files 'contracts/*.yaml' 'contracts/**/*.yaml')"
expected="$(printf 'contracts/agent-policy.yaml\ncontracts/diagnostics.yaml\ncontracts/libc.yaml\ncontracts/safety-levels.yaml\n'; git ls-files 'contracts/bundles/*.yaml' | sort)"
[ "$committed" = "$expected" ] || {
  echo "unexpected tracked contract files:"; diff <(echo "$expected") <(echo "$committed"); exit 1;
}
[ -z "$(git ls-files 'contracts/bundles/' | grep -v '\.yaml$' || true)" ] || {
  echo "non-yaml files tracked under contracts/bundles/"; exit 1;
}

echo "==> contracts: merge rejects duplicate symbols"
dup="$work/dup.yaml"
cat > "$dup" <<'EOF'
schema: cand.api-contract/v1
name: dup-bundle
version: "1.0.0"
symbols:
  - symbol: malloc
    kind: function
    params:
      - index: 0
        effect: no_ownership_effect
    returns:
      ownership: owned
      allocation_family: c-heap
      nullable: true
EOF
set +e
python3 scripts/contracts/merge_contracts.py --out "$work/dup-out.yaml" \
  --bundle contracts/libc.yaml --bundle "$dup" >/dev/null 2>&1
status=$?
set -e
[ "$status" -ne 0 ] || { echo "duplicate symbol was accepted by the merge"; exit 1; }
[ ! -s "$work/dup-out.yaml" ] || { echo "merge wrote output despite duplicate"; exit 1; }

echo "==> contracts: merge rejects malformed bundles"
bad="$work/bad.yaml"
printf 'schema: cand.api-contract/v1\nname: bad\nversion: "1.0.0"\n' > "$bad"
set +e
python3 scripts/contracts/merge_contracts.py --out "$work/bad-out.yaml" \
  --bundle contracts/libc.yaml --bundle "$bad" >/dev/null 2>&1
status=$?
set -e
[ "$status" -ne 0 ] || { echo "bundle without symbols was accepted"; exit 1; }

echo "==> contracts: digest mutation changes the recorded digest"
python3 scripts/contracts/merge_contracts.py --out "$work/m1.yaml" --digests | tail -1 > "$work/d1.txt"
cp contracts/bundles/libc-ctype.yaml "$work/ctype-backup.yaml"
printf '\n# mutation\n' >> contracts/bundles/libc-ctype.yaml
python3 scripts/contracts/merge_contracts.py --out "$work/m2.yaml" --digests | tail -1 > "$work/d2.txt"
mv "$work/ctype-backup.yaml" contracts/bundles/libc-ctype.yaml
! cmp -s "$work/d1.txt" "$work/d2.txt" || { echo "digest did not change on mutation"; exit 1; }
# restore must reproduce the original merged bytes
python3 scripts/contracts/merge_contracts.py --out "$work/m3.yaml" >/dev/null
cmp "$work/m1.yaml" "$work/m3.yaml" || { echo "restore did not reproduce merged bytes"; exit 1; }

check() {
  local expected="$1" file="$2" std="$3"; shift 3
  local output status
  set +e
  output="$($cand check --format=json --contracts="$merged" "$file" "$@" -- $std 2>/dev/null)"; status=$?
  set -e
  grep -Fq '"result": "'"$expected"'"' <<<"$output" || {
    echo "unexpected result for $file (expected $expected)"; exit 1;
  }
}

echo "==> contracts: conformance fixtures (positive)"
check pass tests/contracts/fixtures/libc_conformance_safe.c "-std=c11 -D_POSIX_C_SOURCE=200809L"
check pass tests/contracts/fixtures/memchr_borrowed_return_safe.c -std=c11
check pass tests/contracts/fixtures/socket_borrow_safe.c "-std=c11 -D_POSIX_C_SOURCE=200809L"

echo "==> contracts: conformance fixtures (adversarial / fail-closed)"
check fail tests/contracts/fixtures/ctype_scalar_uaf.c -std=c11
check fail tests/contracts/fixtures/close_member_uaf.c "-std=c11 -D_POSIX_C_SOURCE=200809L"
check fail tests/contracts/fixtures/memchr_borrowed_return_uaf.c -std=c11
check incomplete tests/contracts/fixtures/realloc_use_after.c -std=c11
check fail tests/contracts/fixtures/free_then_use_malloc.c -std=c11
check incomplete tests/contracts/fixtures/snprintf_variadic_escape.c -std=c11
check incomplete tests/contracts/fixtures/excluded_symbols_uncontracted.c "-std=c11 -D_POSIX_C_SOURCE=200809L"

echo "==> contracts: positive fixtures are INCOMPLETE without contracts"
for f in libc_conformance_safe.c socket_borrow_safe.c; do
  set +e
  output="$($cand check --format=json "tests/contracts/fixtures/$f" -- -std=c11 -D_POSIX_C_SOURCE=200809L 2>/dev/null)"; status=$?
  set -e
  grep -Fq '"result": "incomplete"' <<<"$output" || {
    echo "$f was not fail-closed without contracts"; exit 1;
  }
done

echo "contracts: all conformance checks passed"
