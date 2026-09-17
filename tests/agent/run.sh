#!/usr/bin/env bash
set -euo pipefail

cand_bin="$(realpath "$1")"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

seed_repo() {
  local dir="$1"
  mkdir -p "$dir"
  cp "$repo/tests/agent/app-broken.c" "$dir/app.c"
  cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"
  git -C "$dir" init -q -b main
  git -C "$dir" config user.name CAND-test
  git -C "$dir" config user.email cand-test@example.invalid
  git -C "$dir" add app.c cand-policy.json
  git -C "$dir" commit -qm baseline
  git -C "$dir" update-ref refs/remotes/origin/main HEAD
}

run_check() {
  (cd "$1" && CAND_TRUSTED_BASE_SHA="$(git rev-parse origin/main)" "$cand_bin" check --agent --base origin/main --policy cand-policy.json "$2" "${@:3}" -- -std=c11)
}

run_verify() {
  (cd "$1" && CAND_TRUSTED_BASE_SHA="$(git rev-parse origin/main)" "$cand_bin" evidence verify "$2")
}

expect_result() {
  local expected="$1" dir="$2" source="$3"; shift 3
  local output rc actual
  set +e
  output="$(run_check "$dir" "$source" "$@" 2>/dev/null)"
  rc=$?
  set -e
  actual="$(jq -r .result <<<"$output")"
  [[ "$actual" == "$expected" ]] || { echo "expected $expected, got $actual (exit $rc)"; exit 1; }
  printf '%s: %s\n' "$source" "$actual"
}

dir="$tmp/repair"
seed_repo "$dir"
expect_result fail "$dir" app.c
cp "$repo/tests/agent/app-good.c" "$dir/app.c"
expect_result pass "$dir" app.c --emit-evidence evidence-a.json
run_check "$dir" app.c --emit-evidence evidence-a.json > "$dir/agent-check.json"
python3 "$repo/tests/agent/schema_test.py" "$dir/evidence-a.json" "$dir/agent-check.json"
cp "$dir/evidence-a.json" "$dir/evidence-b.json"
expect_result pass "$dir" app.c --emit-evidence evidence-b.json
cmp -s "$dir/evidence-a.json" "$dir/evidence-b.json"
(run_verify "$dir" evidence-a.json | jq -e '.result == "valid"' >/dev/null)
echo 'deterministic evidence: PASS'

mkdir -p "$dir/contracts"
cp "$repo/tests/interprocedural/candidate_self_trust.yaml" "$dir/contracts/candidate.yaml"
expect_result fail-policy "$dir" app.c --contracts=contracts/candidate.yaml
echo 'self-authored trusted contract: BLOCKED'

contract_dir="$tmp/contract"
mkdir -p "$contract_dir/contracts"
cp "$repo/tests/agent/app-contract.c" "$contract_dir/app.c"
cp "$repo/tests/interprocedural/vendor.yaml" "$contract_dir/contracts/vendor.yaml"
contract_hash="$(sha256sum "$contract_dir/contracts/vendor.yaml" | cut -d' ' -f1)"
jq --arg hash "$contract_hash" '.contracts.trusted = [{"path":"contracts/vendor.yaml","sha256":$hash,"trust_class":"reviewed"}]' \
  "$repo/tests/agent/policy-template.json" > "$contract_dir/cand-policy.json"
git -C "$contract_dir" init -q -b main
git -C "$contract_dir" config user.name CAND-test
git -C "$contract_dir" config user.email cand-test@example.invalid
git -C "$contract_dir" add app.c cand-policy.json contracts/vendor.yaml
git -C "$contract_dir" commit -qm 'reviewed contract baseline'
git -C "$contract_dir" update-ref refs/remotes/origin/main HEAD
contract_output="$(run_check "$contract_dir" app.c --contracts=contracts/vendor.yaml --emit-evidence contract-evidence.json 2>/dev/null)"
[[ "$(jq -r .result <<<"$contract_output")" == pass ]]
echo 'pinned trusted contract: PASS'
printf '\n# mutation\n' >> "$contract_dir/contracts/vendor.yaml"
set +e
run_verify "$contract_dir" contract-evidence.json > "$contract_dir/contract-stale.json"
contract_stale_rc=$?
set -e
[[ "$contract_stale_rc" == 1 ]]
jq -e '.result == "stale"' "$contract_dir/contract-stale.json" >/dev/null
echo 'trusted contract mutation: DETECTED'

cp "$dir/app.c" "$dir/app.saved"
printf '\n/* CAND_UNSAFE */\n' >> "$dir/app.c"
expect_result fail-policy "$dir" app.c
cp "$dir/app.saved" "$dir/app.c"
printf '#define CAND_UNSAFE 1\n' > "$dir/policy-header.h"
printf '\n#include "policy-header.h"\n' >> "$dir/app.c"
expect_result fail-policy "$dir" app.c
cp "$dir/app.saved" "$dir/app.c"
rm "$dir/policy-header.h"
printf '\n/* CAND_SUPPRESS */\n' >> "$dir/app.c"
expect_result fail-policy "$dir" app.c
cp "$dir/app.saved" "$dir/app.c"

jq '.budgets.new_unsafe_boundaries = 1' "$dir/cand-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/cand-policy.json"
set +e
(cd "$dir" && "$cand_bin" policy diff --base origin/main --format json > diff.json)
diff_rc=$?
set -e
[[ "$diff_rc" == 4 ]]
jq -e '.classification == "PROOF_WEAKENING" and .result == "fail"' "$dir/diff.json" >/dev/null
expect_result fail-policy "$dir" app.c
echo 'unsafe budget weakening: BLOCKED'

cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"
jq '.scope.files = ["other.c"]' "$dir/cand-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/cand-policy.json"
expect_result fail-policy "$dir" app.c
echo 'checked-scope reduction: BLOCKED'

cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"
set +e
(cd "$dir" && "$cand_bin" check --agent --base origin/main --policy cand-policy.json app.c -- -std=c11 -DNO_UAF > frontend.json)
frontend_rc=$?
set -e
[[ "$frontend_rc" == 4 ]]
jq -e '.result == "fail-policy"' "$dir/frontend.json" >/dev/null
echo 'frontend argument substitution: BLOCKED'

cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"
jq '.frontend.arguments = ["-I/tmp"]' "$dir/cand-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/cand-policy.json"
expect_result fail-policy "$dir" app.c
echo 'external include path: BLOCKED'

cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"
set +e
(cd "$dir" && CPATH=/tmp CAND_TRUSTED_BASE_SHA="$(git rev-parse origin/main)" "$cand_bin" check --agent --base origin/main --policy cand-policy.json app.c -- -std=c11 > env.json)
env_rc=$?
set -e
[[ "$env_rc" == 4 ]]
jq -e '.result == "fail-policy"' "$dir/env.json" >/dev/null
echo 'ambient include environment: BLOCKED'

jq '.base_ref = "HEAD"' "$dir/cand-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/cand-policy.json"
expect_result fail-policy "$dir" app.c
cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"

cp "$dir/evidence-a.json" "$dir/evidence-tampered.json"
jq '.result = "fail"' "$dir/evidence-tampered.json" > "$dir/evidence.next"
mv "$dir/evidence.next" "$dir/evidence-tampered.json"
set +e
(run_verify "$dir" evidence-tampered.json > "$dir/tamper.json")
tamper_rc=$?
set -e
[[ "$tamper_rc" == 2 ]]
jq -e '.result == "tampered"' "$dir/tamper.json" >/dev/null
echo 'manual evidence edit: DETECTED'

python3 - "$dir/evidence-a.json" "$dir/evidence-forged.json" <<'PY'
import hashlib, json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    evidence = json.load(stream)
evidence["semantic_result"] = "fail"
evidence.pop("integrity_sha256")
payload = json.dumps(evidence, ensure_ascii=False, sort_keys=True, indent=2)
evidence["integrity_sha256"] = hashlib.sha256(payload.encode()).hexdigest()
with open(sys.argv[2], "w", encoding="utf-8") as stream:
    json.dump(evidence, stream, ensure_ascii=False, sort_keys=True, indent=2)
    stream.write("\n")
PY
set +e
run_verify "$dir" evidence-forged.json > "$dir/forged.json"
forged_rc=$?
set -e
[[ "$forged_rc" == 2 ]]
jq -e '.result == "tampered"' "$dir/forged.json" >/dev/null
echo 'recomputed evidence forgery: DETECTED'

cp "$cand_bin" "$dir/cand-mutated"
printf 'tamper' >> "$dir/cand-mutated"
chmod +x "$dir/cand-mutated"
set +e
(cd "$dir" && CAND_TRUSTED_BASE_SHA="$(git rev-parse origin/main)" ./cand-mutated evidence verify evidence-a.json > binary-stale.json)
binary_stale_rc=$?
set -e
[[ "$binary_stale_rc" == 1 ]]
jq -e '.result == "stale"' "$dir/binary-stale.json" >/dev/null
echo 'verifier binary substitution: DETECTED'

jq '.budgets.new_unsafe_boundaries = 1' "$dir/cand-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/cand-policy.json"
set +e
(run_verify "$dir" evidence-a.json > "$dir/policy-stale.json")
policy_stale_rc=$?
set -e
[[ "$policy_stale_rc" == 1 ]]
jq -e '.result == "stale"' "$dir/policy-stale.json" >/dev/null
echo 'policy mutation: DETECTED'

cp "$repo/tests/agent/policy-template.json" "$dir/cand-policy.json"
printf '\n/* source mutation */\n' >> "$dir/app.c"
set +e
(run_verify "$dir" evidence-a.json > "$dir/stale.json")
stale_rc=$?
set -e
[[ "$stale_rc" == 1 ]]
jq -e '.result == "stale"' "$dir/stale.json" >/dev/null
echo 'source mutation: DETECTED'

cp "$dir/app.saved" "$dir/app.c"
set +e
(cd "$dir" && CAND_TRUSTED_BASE_SHA="$(git rev-parse origin/main)" "$cand_bin" check --agent --base HEAD --policy cand-policy.json app.c -- -std=c11 > base.json)
base_rc=$?
set -e
[[ "$base_rc" == 4 ]]
jq -e '.result == "fail-policy"' "$dir/base.json" >/dev/null
echo 'base substitution: BLOCKED'

cp "$repo/tests/agent/policy-template.json" "$dir/base-policy.json"
cp "$repo/tests/agent/policy-template.json" "$dir/head-policy.json"
jq '.budgets.new_unsafe_boundaries = 1' "$dir/base-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/base-policy.json"
(cd "$dir" && "$cand_bin" policy diff --base-policy base-policy.json --head-policy head-policy.json --format json > strengthening.json)
jq -e '.classification == "PROOF_STRENGTHENING" and .result == "pass"' "$dir/strengthening.json" >/dev/null
jq '.contracts.trusted += [{"path":"contracts/vendor.yaml","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","trust_class":"reviewed"}]' "$dir/head-policy.json" > "$dir/policy.next"
mv "$dir/policy.next" "$dir/head-policy.json"
set +e
(cd "$dir" && "$cand_bin" policy diff --base-policy base-policy.json --head-policy head-policy.json --format json > contracts-diff.json)
contract_diff_rc=$?
set -e
[[ "$contract_diff_rc" == 4 ]]
jq -e '.classification == "REVIEW_REQUIRED"' "$dir/contracts-diff.json" >/dev/null
echo 'contract authority change: REVIEW_REQUIRED'

echo 'agent verification tests passed'
