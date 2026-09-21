#!/usr/bin/env bash
# PASS-path drift guard (C&1 Proof Plan, Phase A).
#
# Asserts that the PASS-emitting sites in src/cand.cpp still match the
# inventory in docs/CAND1-PASS-PATH-AUDIT.md. Any new, removed, or modified
# path to a "pass" verdict fails this guard until the audit document and the
# baselines below are updated in the same change set.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$root/src/cand.cpp"
audit_doc="$root/docs/CAND1-PASS-PATH-AUDIT.md"

fail() {
    echo "pass-path guard: $1" >&2
    echo "pass-path guard: update docs/CAND1-PASS-PATH-AUDIT.md and the baselines in scripts/pass-path-guard.sh in the same change set" >&2
    exit 1
}

# Baseline: total occurrences of the "pass" string literal in src/cand.cpp.
readonly PASS_LITERAL_COUNT=7

count_pass_literals() {
    grep -o '"pass"' "$source_file" | wc -l
}

[[ -f "$audit_doc" ]] || fail "audit document $audit_doc is missing"
[[ "$(count_pass_literals)" -eq "$PASS_LITERAL_COUNT" ]] ||
    fail "expected $PASS_LITERAL_COUNT \"pass\" literal occurrences in src/cand.cpp, found $(count_pass_literals)"

# Site 1: the semantic verdict must remain the exact fail/incomplete/pass
# ternary over findings and unsupported obligations.
grep -qF 'hasFindings() ? "fail" : (hasUnsupported() ? "incomplete" : "pass")' "$source_file" ||
    fail "semantic verdict expression (audit Site 1) no longer matches"

# Site 2 gate: canEmitCand1Pass must retain all eight gate conditions.
grep -qF 'bool canEmitCand1Pass(const Collector &collector, const AgentPolicyState &state,' "$source_file" ||
    fail "canEmitCand1Pass definition (audit Site 2) is missing"
for condition in \
    'collector.hasFindings() || collector.hasUnsupported() ||' \
    'collector.hasFrontendError() || collector.hasContractError() ||' \
    'collector.unsupportedTransportCount() != 0' \
    'if (!evidence_bound || state.policy_failed || state.review_required ||' \
    'state.delta.weakened || state.delta.review_required) return false;' \
    'if (!state.toolchain_supported) return false;' \
    'state.policy.profile != "generated" || state.policy.safety_level != "cand1"' \
    'state.policy.scope_files.empty() || state.policy.sha256.empty()' \
    'if (trust != "builtin" && trust != "verified" && trust != "reviewed") return false;' \
    'return state.unsafe_boundaries == 0 && state.suppressions == 0;'; do
    grep -qF "$condition" "$source_file" ||
        fail "canEmitCand1Pass gate condition no longer present: $condition"
done

# Site 4: the attestation consumer must still reject pass claims that are not
# backed by both semantic and policy pass without weakening.
grep -qF '(semantic_result->str() != "pass" || policy_result->str() != "pass" || *weakened))' "$source_file" ||
    fail "attestation rejector condition (audit Site 4) no longer matches"

# Fail-closed exit: non-agent cand1 mode must still coerce the verdict.
grep -qF 'requires trusted generated evidence and policy bindings' "$source_file" ||
    fail "non-agent cand1 coercion message no longer present"

echo "pass-path guard: PASS-verdict sites match the audited inventory"
