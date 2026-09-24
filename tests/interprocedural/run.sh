#!/usr/bin/env bash
set -euo pipefail
cand="$1"
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# Reviewed bundles merged by the deterministic tool (same flow as the
# CVE-replay harness and tests/contracts). See scripts/contracts/merge_contracts.py.
merged="$work/merged-contracts.yaml"
python3 scripts/contracts/merge_contracts.py --out "$merged" >/dev/null
check() {
  local expected="$1" file="$2"; shift 2
  local output status
  set +e
  output="$($cand check --format=json "$file" "$@" -- -std=c11 2>/dev/null)"; status=$?
  set -e
  grep -Fq '"result": "'"$expected"'"' <<<"$output" || { echo "unexpected result for $file"; exit 1; }
}
check pass tests/interprocedural/allocator_wrapper_safe.c
check pass tests/interprocedural/local_allocator_wrapper_safe.c
check pass tests/interprocedural/nested_allocator_wrapper_safe.c
check pass tests/interprocedural/member_summary_safe.c
check pass tests/interprocedural/borrow_wrapper_safe.c
check fail tests/interprocedural/member_summary_uaf.c
check incomplete tests/interprocedural/ambiguous_destroy_argument_incomplete.c
set +e
"$cand" check --contracts=tests/interprocedural/malformed_numeric_contract.yaml --format=json tests/interprocedural/unknown_external.c >/dev/null 2>&1
status=$?
set -e
[[ "$status" == 2 ]] || { echo "malformed numeric contract was accepted"; exit 1; }
for contract in tests/interprocedural/candidate_self_trust.yaml tests/interprocedural/missing_contract_metadata.yaml tests/interprocedural/malformed_contract_indentation.yaml; do
  set +e
  "$cand" check --contracts="$contract" --format=json tests/interprocedural/unknown_external.c >/dev/null 2>&1
  status=$?
  set -e
  [[ "$status" == 2 ]] || { echo "invalid contract was accepted: $contract"; exit 1; }
done
check incomplete tests/interprocedural/dead_local_return_incomplete.c
check fail tests/interprocedural/destructor_wrapper_uaf.c
check fail tests/interprocedural/double_destructor_wrapper.c
check fail tests/interprocedural/borrow_wrapper_uaf.c
check fail tests/interprocedural/array_parameter_uaf.c
check fail tests/interprocedural/member_parameter_uaf.c
check fail tests/interprocedural/borrow_chain_argument_remap_uaf.c
check fail tests/interprocedural/reordered_destroy_chain_uaf.c
check incomplete tests/interprocedural/out_parameter_incomplete.c
check incomplete tests/interprocedural/reassigned_borrow_origin_incomplete.c
check incomplete tests/interprocedural/reassigned_borrow_origin_helper_incomplete.c
check incomplete tests/interprocedural/reassigned_via_outparam_incomplete.c
check fail tests/interprocedural/reassigned_origin_direct_return_uaf.c
check fail tests/interprocedural/local_alias_parameter_uaf.c
check pass tests/interprocedural/read_parameter_safe.c
check incomplete tests/interprocedural/unknown_external.c
check incomplete tests/interprocedural/unknown_external_ignored_return.c
check incomplete tests/interprocedural/unknown_external_pointer_argument.c
check pass tests/interprocedural/unknown_external.c --contracts=tests/interprocedural/vendor.yaml
check fail tests/interprocedural/trusted_external_destroy_uaf.c --contracts=tests/interprocedural/vendor_lifetimes.yaml
check pass tests/interprocedural/trusted_external_view_safe.c --contracts=tests/interprocedural/vendor_lifetimes.yaml
check fail tests/interprocedural/trusted_external_view_uaf.c --contracts=tests/interprocedural/vendor_lifetimes.yaml
check incomplete tests/interprocedural/contract_allocator_wrapper.c
check pass tests/interprocedural/contract_allocator_wrapper.c --contracts=tests/interprocedural/vendor.yaml
check pass tests/interprocedural/consumes_contract_safe.c --contracts=tests/interprocedural/consumes.yaml
check incomplete tests/interprocedural/contract_body_conflict.c --contracts=tests/interprocedural/conflict.yaml
check incomplete tests/interprocedural/mixed_return_incomplete.c
check incomplete tests/interprocedural/realloc_incomplete.c
check incomplete tests/interprocedural/realloc_contract_incomplete.c --contracts=contracts/libc.yaml
check incomplete tests/interprocedural/global_retention_incomplete.c
check incomplete tests/interprocedural/conditional_destroy_conservative.c
check incomplete tests/interprocedural/consumes_contract_incomplete.c --contracts=tests/interprocedural/consumes.yaml
check incomplete tests/interprocedural/consumes_wrapper_incomplete.c --contracts=tests/interprocedural/consumes.yaml
check incomplete tests/interprocedural/consumes_then_use_incomplete.c --contracts=tests/interprocedural/consumes.yaml
check incomplete tests/interprocedural/recursive_return_incomplete.c
check incomplete tests/interprocedural/mutual_recursive_return_incomplete.c

for safe in parameter_borrow_read_safe.c parameter_destroy_once_safe.c parameter_owner_read_safe.c; do
  check pass "tests/interprocedural/$safe"
done
for invalid in \
  parameter_borrow_free_invalid.c \
  parameter_destroy_twice_fail.c \
  parameter_destroy_then_read_fail.c \
  parameter_destroy_then_write_fail.c \
  parameter_owner_destroy_then_read_fail.c \
  parameter_owner_destroy_then_write_fail.c \
  parameter_owner_double_destroy_fail.c \
  parameter_owner_alias_uaf_fail.c \
  parameter_owner_alias_assignment_fail.c \
  parameter_owner_move_then_use_fail.c; do
  check fail "tests/interprocedural/$invalid"
done
# The parameter-identity repair tracks summary-Unknown parameters as live-at-entry
# objects with no authority. Conditional and loop destruction of such a parameter
# followed by a use therefore joins to MaybeDead and is reported as a `possible`
# use-after-destruction FAIL, exactly matching the qualified local-variable
# semantics (see tests/cfg/branch_null_or_free_possible_uaf.c). This is a
# reviewed verdict STRENGTHENING (INCOMPLETE -> FAIL(possible)); it can never
# pass, and is stronger than the previous fail-closed INCOMPLETE.
check fail tests/interprocedural/parameter_owner_conditional_destroy_fail.c
check fail tests/interprocedural/parameter_owner_loop_destroy_fail.c

# Milestone #54 (ADR-0028): bounded local-alias destruction attribution.
# The core repair turns the CAND-O006 false FAIL into a correct Destroy
# summary; every ambiguous alias shape stays fail-closed; the loop and
# interleaving shapes must never gain a PASS on a use-after. The two
# *_residual_* fixtures pin documented known false FAILs (see
# docs/pilots/ALIAS-STORAGE-PARETO.md section 4) so any future change to
# those boundaries is deliberate.
check pass tests/interprocedural/parameter_alias_destroy_safe.c
check fail tests/interprocedural/parameter_alias_destroy_then_use_fail.c
check fail tests/interprocedural/parameter_alias_destroy_caller_use_fail.c
check fail tests/interprocedural/parameter_alias_destroy_caller_free_fail.c
check fail tests/interprocedural/parameter_alias_double_destroy_fail.c
check incomplete tests/interprocedural/parameter_alias_conditional_destroy_incomplete.c
check incomplete tests/interprocedural/parameter_alias_reassigned_local_incomplete.c
check fail tests/interprocedural/parameter_alias_reassigned_param_residual_fail.c
check fail tests/interprocedural/parameter_alias_transitive_residual_fail.c
check incomplete tests/interprocedural/parameter_alias_conditional_init_incomplete.c
check pass tests/interprocedural/parameter_alias_wrong_param_probe_safe.c
check fail tests/interprocedural/parameter_alias_wrong_param_probe_fail.c
check pass tests/interprocedural/parameter_alias_annotated_borrow_body_authority_safe.c
check fail tests/interprocedural/parameter_alias_loop_destroy_fail.c
check pass tests/interprocedural/parameter_alias_move_interleaving_safe.c
check fail tests/interprocedural/parameter_alias_move_interleaving_useafter_fail.c
check incomplete tests/interprocedural/parameter_alias_unknown_callee_incomplete.c

# Unknown-capability red-team matrix (see parameter_unknown_capability_redteam.c):
# every escape of an unannotated pointer parameter to an opaque callee must be
# reported (never PASS); destruction/transfer stays fail-closed; return-by-param
# stays safe. These are the soundness probes for the parameter-identity repair.
for unknown_case in \
  ESCAPE ESCAPE_READ ESCAPE_FREE_READ COND_FREE_READ ALIAS_FREE_READ \
  DOUBLE_FREE FREE_READ MOVE; do
  expected=incomplete
  case "$unknown_case" in
    ESCAPE|ESCAPE_READ|MOVE) expected=incomplete ;;
    *) expected=fail ;;
  esac
  set +e
  uo="$($cand check --format=json tests/interprocedural/parameter_unknown_capability_redteam.c \
    -- -std=c11 -DCASE_UNKNOWN_$unknown_case 2>/dev/null)"
  us=$?
  set -e
  grep -Fq '"result": "'"$expected"'"' <<<"$uo" || {
    echo "unexpected result for Unknown-capability case $unknown_case (expected $expected)"; exit 1;
  }
done
set +e
uo="$($cand check --format=json tests/interprocedural/parameter_unknown_capability_redteam.c \
  -- -std=c11 -DCASE_UNKNOWN_RETURN_PARAM 2>/dev/null)"
us=$?
set -e
grep -Fq '"result": "pass"' <<<"$uo" || {
  echo "return-by-parameter case was not PASS"; exit 1;
}

# Reviewed libc borrow bundles (contracts/bundles/, merged): with the bundles
# active, memcpy/memmove/memset/memcmp/snprintf operating on tracked heap
# allocations are modeled as borrows (decidable, non-retaining) and the
# fixture PASSES. This guard pins both that the merged contract file loads
# and that it clears the opaque-call obligation without weakening any
# destroy/retention soundness.
check pass tests/interprocedural/libc_borrow_bundle_safe.c --contracts="$merged"

# Variadic-argument escape matrix (see variadic_argument_escape.c): tracked
# pointers passed at argument positions beyond the callee's modelled parameter
# list must be reported as escapes, never PASS. Regression guard for the
# variadic false-PASS incident (the summary path previously skipped these
# positions entirely, including on v0.2.0).
for va_case in ESCAPE DEAD; do
  set +e
  va_output="$($cand check --format=json tests/interprocedural/variadic_argument_escape.c \
    -- -std=c11 -DCASE_VA_$va_case 2>/dev/null)"
  va_status=$?
  set -e
  grep -Fq '"result": "incomplete"' <<<"$va_output" || {
    echo "unexpected result for variadic case $va_case (expected incomplete)"; exit 1;
  }
done
set +e
va_output="$($cand check --format=json tests/interprocedural/variadic_argument_escape.c \
  --contracts="$merged" -- -std=c11 -DCASE_VA_SNPRINTF 2>/dev/null)"
va_status=$?
set -e
grep -Fq '"result": "incomplete"' <<<"$va_output" || {
  echo "unexpected result for variadic case SNPRINTF (expected incomplete)"; exit 1;
}

for sibling_case in ARRAY MULTI NULL NULL_LIVE BORROW_ALIAS OWNED_RETURN; do
  set +e
  sibling_output="$($cand check --format=json tests/interprocedural/parameter_lifetime_siblings.c \
    -- -std=c11 -DSIB_$sibling_case 2>/dev/null)"
  sibling_status=$?
  set -e
  grep -Fq '"result": "pass"' <<<"$sibling_output" || {
    echo "sibling safe case was not PASS: SIB_$sibling_case"; exit 1;
  }
done
for sibling_case in TYPEDEF CONST ALIAS_PARAMS SAME; do
  set +e
  sibling_output="$($cand check --format=json tests/interprocedural/parameter_lifetime_siblings.c \
    -- -std=c11 -DSIB_$sibling_case 2>/dev/null)"
  sibling_status=$?
  set -e
  grep -Fq '"result": "fail"' <<<"$sibling_output" || {
    echo "sibling invalid case was not FAIL: SIB_$sibling_case"; exit 1;
  }
done
set +e
sibling_output="$($cand check --format=json tests/interprocedural/parameter_lifetime_siblings.c \
  --level=cand1 -- -std=c11 -DSIB_BORROW_MOVE 2>/dev/null)"
sibling_status=$?
set -e
grep -Fq '"result": "incomplete"' <<<"$sibling_output" || {
  echo "borrowed parameter move was not fail-closed"; exit 1;
}

for case in A B C D E F G H I J K L N O; do
  expected=incomplete
  case "$case" in
    A|B) expected=pass ;;
    C|D|E|F|G|H|I|J|K|L) expected=fail ;;
    # N/O (conditional/loop destruction of a parameter, then use) are reported
    # as `possible` use-after-destruction FAIL since the parameter-identity
    # repair; see the note at the parameter_owner_*_destroy_fail expectations.
    N|O) expected=fail ;;
  esac
  set +e
  output="$($cand check --format=json tests/interprocedural/parameter_lifetime_redteam.c \
    -- -std=c11 -DCASE_$case 2>/dev/null)"
  case_status=$?
  set -e
  grep -Fq '"result": "'"$expected"'"' <<<"$output" || {
    echo "unexpected result for parameter red-team CASE_$case"; exit 1;
  }
done
set +e
vendor_output="$($cand check --contracts=tests/interprocedural/parameter_lifetime_redteam.yaml \
  --format=json tests/interprocedural/parameter_lifetime_redteam.c -- -std=c11 -DCASE_M 2>/dev/null)"
vendor_status=$?
set -e
grep -Fq '"result": "fail"' <<<"$vendor_output" || {
  echo "trusted parameter destructor did not fail after use"; exit 1;
}

check pass tests/interprocedural/contract_partial_compatible.c \
  --contracts=tests/interprocedural/contract_partial_compatible.yaml
set +e
conflict_output="$($cand check --format=json tests/interprocedural/contract_partial_conflicts.c \
  --contracts=tests/interprocedural/contract_partial_conflicts.yaml -- -std=c11 2>/dev/null)"
conflict_status=$?
set -e
[[ "$conflict_status" == 3 ]] || {
  echo "partial contract conflicts returned unexpected exit: $conflict_status"; exit 1;
}
grep -Fq '"result": "incomplete"' <<<"$conflict_output" || {
  echo "partial contract conflicts unexpectedly became decidable"; exit 1;
}
for reason in "explicit no-effect mismatch" "return ownership mismatch" \
             "param effect mismatch" "unknown body effect" \
             "return borrow-origin mismatch" \
             "conditional/unrepresentable body behavior" \
             "annotation/body mismatch"; do
  grep -Fq '"conflict_reason": "'"$reason"'"' <<<"$conflict_output" || {
    echo "missing contract conflict reason: $reason"; exit 1;
  }
done
grep -Fq '"parameter_index": 0' <<<"$conflict_output" || {
  echo "contract conflict parameter index missing"; exit 1;
}
check incomplete tests/interprocedural/external_annotation_only.c
echo "interprocedural OK"
check incomplete tests/interprocedural/compound_origin_conditional_incomplete.c
check incomplete tests/interprocedural/compound_origin_condptr_incomplete.c
check fail tests/interprocedural/compound_origin_comma_uaf.c
check incomplete tests/interprocedural/compound_origin_subscript_incomplete.c
check pass tests/interprocedural/compound_origin_controls_safe.c
check fail tests/interprocedural/compound_origin_controls_detect.c
check incomplete tests/interprocedural/compound_origin_callargs_incomplete.c
check pass tests/interprocedural/conditional_join_borrow_safe.c
check pass tests/interprocedural/conditional_join_none_safe.c
check incomplete tests/interprocedural/conditional_join_destroy_incomplete.c
check pass tests/interprocedural/conditional_join_uncond_destroy_safe.c
check fail tests/interprocedural/conditional_join_uaf_detect.c
check incomplete tests/interprocedural/conditional_join_branch_conflict_incomplete.c

# Milestone #39: reviewed declaration-site annotation propagation.
# Candidate-only annotations (no review manifest) never gain PASS
# authority: the annotated external boundary stays INCOMPLETE.
check incomplete tests/interprocedural/annotation_review_owned_return.c
check incomplete tests/interprocedural/annotation_review_borrow_return_safe.c
check incomplete tests/interprocedural/annotation_review_destroy_param_safe.c
check incomplete tests/interprocedural/annotation_review_takes_param_safe.c
check incomplete tests/interprocedural/annotation_review_borrow_param_safe.c
# With a matching reviewed manifest the boundary resolves like a contract.
check pass tests/interprocedural/annotation_review_owned_return.c --annotation-review=tests/interprocedural/annotation_review_owned_return.yaml
check incomplete tests/interprocedural/annotation_review_owned_return.c --annotation-review=tests/interprocedural/annotation_review_owned_return_mismatch.yaml
check pass tests/interprocedural/annotation_review_borrow_return_safe.c --annotation-review=tests/interprocedural/annotation_review_borrow_return.yaml
check fail tests/interprocedural/annotation_review_borrow_return_escape.c --annotation-review=tests/interprocedural/annotation_review_borrow_return.yaml
check pass tests/interprocedural/annotation_review_destroy_param_safe.c --annotation-review=tests/interprocedural/annotation_review_destroy_param.yaml
check fail tests/interprocedural/annotation_review_destroy_param_uaf.c --annotation-review=tests/interprocedural/annotation_review_destroy_param.yaml
check pass tests/interprocedural/annotation_review_takes_param_safe.c --annotation-review=tests/interprocedural/annotation_review_takes_param.yaml
# Consume-then-use is INCOMPLETE in both modes (access-unknown-ownership-state);
# pinned to the identical contract-twin verdict (H1 parity).
check incomplete tests/interprocedural/annotation_review_takes_param_double_use.c --annotation-review=tests/interprocedural/annotation_review_takes_param.yaml
check pass tests/interprocedural/annotation_review_borrow_param_safe.c --annotation-review=tests/interprocedural/annotation_review_borrow_param.yaml
check pass tests/interprocedural/annotation_review_callback_borrow.c --annotation-review=tests/interprocedural/annotation_review_callback_borrow.yaml
check pass tests/interprocedural/contract_callback_borrow.c --contracts=tests/interprocedural/contract_callback_borrow.yaml
# Conflicting annotations across redeclarations can never match a single
# reviewed manifest entry; fail closed.
check incomplete tests/interprocedural/annotation_review_redecl_conflict_return.c --annotation-review=tests/interprocedural/annotation_review_redecl_conflict.yaml
check incomplete tests/interprocedural/annotation_review_redecl_conflict_param.c --annotation-review=tests/interprocedural/annotation_review_redecl_conflict.yaml
# Reviewed annotations merge with reviewed contracts: agreement keeps the
# contract's provenance, disagreement fails closed.
check pass tests/interprocedural/annotation_review_contract_agree.c --contracts=tests/interprocedural/annotation_review_contract_companion.yaml --annotation-review=tests/interprocedural/annotation_review_contract_merge.yaml
check incomplete tests/interprocedural/annotation_review_contract_disagree.c --contracts=tests/interprocedural/annotation_review_contract_companion.yaml --annotation-review=tests/interprocedural/annotation_review_contract_merge.yaml
# Scope guards: pointer-to-pointer shapes, unspecified-parameter (K&R)
# declarations, realloc, malformed indices, and indirect calls stay
# fail-closed even with a matching manifest.
check incomplete tests/interprocedural/annotation_review_ptr_ptr_param.c --annotation-review=tests/interprocedural/annotation_review_scope_guards.yaml
check incomplete tests/interprocedural/annotation_review_ptr_ptr_return.c --annotation-review=tests/interprocedural/annotation_review_scope_guards.yaml
check incomplete tests/interprocedural/annotation_review_knr.c --annotation-review=tests/interprocedural/annotation_review_edge_guards.yaml
check incomplete tests/interprocedural/annotation_review_realloc.c --annotation-review=tests/interprocedural/annotation_review_edge_guards.yaml
check incomplete tests/interprocedural/annotation_review_malformed_borrow_index.c
check incomplete tests/interprocedural/annotation_review_indirect.c
# Same-TU prototype annotations follow the existing builder path (body
# precedence, combineReturn conflict); unchanged pins.
check incomplete tests/interprocedural/redecl_annotation_body_conflict.c
check pass tests/interprocedural/redecl_annotation_body_agree.c
# A malformed annotation review manifest is a hard input error.
set +e
"$cand" check --annotation-review=tests/interprocedural/annotation_review_malformed.yaml --format=json tests/interprocedural/annotation_review_owned_return.c >/dev/null 2>&1
status=$?
set -e
[[ "$status" == "2" ]] || { echo "invalid annotation review manifest was accepted"; exit 1; }

# --- Milestone #41: bounded produces_out_owner contracts (Gate B,
# fixtures-first). Every check runs at --level=cand1 with the
# --pointer-output-contracts modifier -- the non-authoritative
# enablement path for fixtures and measurement. Agent-mode authority
# (the policy features.pointer_output_contracts flag, mismatch
# handling, and canEmitCand1Pass suppression) is exercised in
# tests/cand1/e/policy_attacks.py. At non-agent cand1 the report-level
# result word is only fail|incomplete, so conversion is asserted via
# the obligation list, never by relaxing the result word.
po_run() {
  local file="$1"; shift
  set +e
  po_output="$("$cand" check --level=cand1 --pointer-output-contracts --format=json \
    "$file" "$@" -- -std=c11 2>/dev/null)"
  po_status=$?
  set -e
}
check_po_converted() { # converted-clean: no findings, row gone, feature on
  po_run "$@"
  grep -Fq '"result": "incomplete"' <<<"$po_output" || { echo "unexpected result for $1"; exit 1; }
  grep -Fq '"pointer_output_contracts": true' <<<"$po_output" || { echo "missing feature field for $1"; exit 1; }
  grep -Fq '"unknown-call-with-pointer-output"' <<<"$po_output" && { echo "pointer-output row not converted for $1"; exit 1; }
  grep -Fq '"findings": []' <<<"$po_output" || { echo "unexpected findings for $1"; exit 1; }
}
check_po_unrefined() { # converted but unrefined use of a maybe-produced binding
  po_run "$@"
  grep -Fq '"result": "incomplete"' <<<"$po_output" || { echo "unexpected result for $1"; exit 1; }
  grep -Fq '"pointer_output_contracts": true' <<<"$po_output" || { echo "missing feature field for $1"; exit 1; }
  grep -Fq '"unrefined-out-owner-use"' <<<"$po_output" || { echo "missing unrefined-out-owner-use row for $1"; exit 1; }
  grep -Fq '"findings": []' <<<"$po_output" || { echo "unrefined use must stay INCOMPLETE for $1"; exit 1; }
}
check_po_refused() { # refused call keeps today's obligation
  po_run "$@"
  grep -Fq '"result": "incomplete"' <<<"$po_output" || { echo "unexpected result for $1"; exit 1; }
  grep -Fq '"pointer_output_contracts": true' <<<"$po_output" || { echo "missing feature field for $1"; exit 1; }
  grep -Fq '"unknown-call-with-pointer-output"' <<<"$po_output" || { echo "refused call lost its obligation for $1"; exit 1; }
}
check_po_fail() { # temporal defect on the produced object
  po_run "$@"
  grep -Fq '"result": "fail"' <<<"$po_output" || { echo "expected FAIL for $1"; exit 1; }
  grep -Fq '"pointer_output_contracts": true' <<<"$po_output" || { echo "missing feature field for $1"; exit 1; }
  if grep -Fq '"unknown-call-with-pointer-output"' <<<"$po_output"; then
    echo "pointer-output row not converted for $1"; exit 1
  fi
}
check_po_row() { # converted; one specific unsupported row kind remains
  po_run "$1" "${@:3}"
  grep -Fq '"result": "incomplete"' <<<"$po_output" || { echo "unexpected result for $1"; exit 1; }
  grep -Fq '"pointer_output_contracts": true' <<<"$po_output" || { echo "missing feature field for $1"; exit 1; }
  grep -Fq "\"kind\": \"$2\"" <<<"$po_output" || { echo "missing row kind $2 for $1"; exit 1; }
  if grep -Fq '"unknown-call-with-pointer-output"' <<<"$po_output"; then
    echo "pointer-output row not converted for $1"; exit 1
  fi
}
check_po_v1_matrix() { # v2 (feature+bundle) == v1 (no feature, no bundle)
  local file="$1" bundle="$2"
  python3 - "$cand" "$file" "$bundle" <<'PY' || { echo "v1/v2 matrix mismatch for $file"; exit 1; }
import json, subprocess, sys
cand, path, bundle = sys.argv[1:4]
def semantics(args):
    run = subprocess.run([cand, "check", "--format=json"] + args + [path, "--", "-std=c11"],
                         capture_output=True, text=True)
    doc = json.loads(run.stdout)
    return json.dumps([doc.get("result"), doc.get("findings"), doc.get("unsupported")],
                      sort_keys=True, indent=1)
base = semantics(["--level=cand1"])
feature = semantics(["--level=cand1", "--pointer-output-contracts", "--contracts=" + bundle])
sys.exit(0 if base == feature else 1)
PY
}
po_expect_reject() { # invalid trusted contract -> exit 2
  local bundle="$1" file="$2"
  set +e
  "$cand" check --level=cand1 --pointer-output-contracts --format=json \
    "$file" --contracts="$bundle" -- -std=c11 >/dev/null 2>&1
  local status=$?
  set -e
  [[ "$status" == "2" ]] || { echo "bundle was accepted: $bundle"; exit 1; }
}
PO=tests/interprocedural
PO_MAIN="--contracts=$PO/pointer_output.yaml"
PO_REF="--contracts=$PO/pointer_output_refusals.yaml"
# Conversion: write:always (absent and null pre-states), C1-C4 in
# if/while/for forms, ==/!= and truthiness polarity, !! and
# parenthesis normalization, compound-then-recognized ordering, the
# F1 subject-discipline pair, and the leak-silence pin
# (ordinary-lattice consistency with owned returns).
check_po_converted $PO/pointer_output_write_always.c $PO_MAIN
check_po_converted $PO/pointer_output_write_always_maybe_c4.c $PO_MAIN
check_po_converted $PO/pointer_output_c1_eq.c $PO_MAIN
check_po_converted $PO/pointer_output_c1_truth.c $PO_MAIN
check_po_converted $PO/pointer_output_c1_neg.c $PO_MAIN
check_po_converted $PO/pointer_output_c2_eq.c $PO_MAIN
check_po_converted $PO/pointer_output_c2_truth.c $PO_MAIN
check_po_converted $PO/pointer_output_c3_eq.c $PO_MAIN
check_po_converted $PO/pointer_output_c3_truth.c $PO_MAIN
check_po_converted $PO/pointer_output_c4_truth.c $PO_MAIN
check_po_converted $PO/pointer_output_c4_ne.c $PO_MAIN
check_po_converted $PO/pointer_output_while_c3.c $PO_MAIN
check_po_converted $PO/pointer_output_for_c3.c $PO_MAIN
check_po_converted $PO/pointer_output_double_bang.c $PO_MAIN
check_po_converted $PO/pointer_output_parens.c $PO_MAIN
check_po_converted $PO/pointer_output_compound_then_recognized.c $PO_MAIN
check_po_converted $PO/pointer_output_f1_call_cond.c $PO_MAIN
check_po_converted $PO/pointer_output_f1_dest_guard.c $PO_MAIN
check_po_converted $PO/pointer_output_leak_silent.c $PO_MAIN
# Unrefined use of a maybe-produced binding (including the failure
# edge, the F1 polarity-confusion shape, and a guard whose pending
# entry was killed by an intervening call) is the new fail-closed
# INCOMPLETE obligation.
check_po_unrefined $PO/pointer_output_unrefined_deref.c $PO_MAIN
check_po_unrefined $PO/pointer_output_unrefined_free.c $PO_MAIN
check_po_unrefined $PO/pointer_output_failure_edge_deref.c $PO_MAIN
check_po_unrefined $PO/pointer_output_f1_polarity_confusion.c $PO_MAIN
check_po_unrefined $PO/pointer_output_guard_after_call.c $PO_MAIN
# Temporal defects on produced objects FAIL; sibling parameter effects
# keep the existing trust model; overwrite keeps today's
# tracked-owner-overwrite row.
check_po_fail $PO/pointer_output_consume_then_use.c $PO_MAIN
check_po_fail $PO/pointer_output_double_destroy.c $PO_MAIN
check_po_fail $PO/pointer_output_sibling_consumes.c $PO_MAIN
check_po_row $PO/pointer_output_overwrite_live.c tracked-owner-overwrite $PO_MAIN
# Refusal predicates: every destination shape that is not ADDR-LOCAL,
# pre-state violations, address escape, variadic and realloc-like
# callees, and loop-carried (live-binding) produces. Each keeps
# today's unknown-call-with-pointer-output obligation byte-for-byte
# (v1/v2 matrix).
check_po_refused $PO/pointer_output_dest_param.c $PO_MAIN
check_po_refused $PO/pointer_output_dest_global.c $PO_MAIN
check_po_refused $PO/pointer_output_dest_static.c $PO_MAIN
check_po_refused $PO/pointer_output_dest_field.c $PO_MAIN
check_po_refused $PO/pointer_output_dest_element.c $PO_MAIN
check_po_refused $PO/pointer_output_dest_nested.c $PO_MAIN
check_po_refused $PO/pointer_output_dest_cast.c $PO_MAIN
check_po_refused $PO/pointer_output_pre_state_owner.c $PO_MAIN
check_po_refused $PO/pointer_output_non_null_init.c $PO_MAIN
check_po_refused $PO/pointer_output_address_escape.c $PO_MAIN
check_po_refused $PO/pointer_output_loop_carried_live.c $PO_MAIN
# Produces inside loops: the back-edge-joined pre-state is MaybeNull or
# Unknown even when each iteration fully consumes the object, so the
# acceptance predicate refuses the call [2.2.3/R9].
check_po_refused $PO/pointer_output_while_c1.c $PO_MAIN
check_po_refused $PO/pointer_output_do_c4.c $PO_MAIN
check_po_refused $PO/pointer_output_loop_complete.c $PO_MAIN
check_po_refused $PO/pointer_output_variadic_callee.c $PO_REF
check_po_refused $PO/pointer_output_realloc_name.c $PO_REF
for po_fixture in pointer_output_dest_param pointer_output_dest_global \
  pointer_output_dest_static pointer_output_dest_field \
  pointer_output_dest_element pointer_output_dest_nested \
  pointer_output_dest_cast pointer_output_pre_state_owner \
  pointer_output_non_null_init pointer_output_address_escape \
  pointer_output_loop_carried_live pointer_output_while_c1 \
  pointer_output_do_c4 pointer_output_loop_complete; do
  check_po_v1_matrix "$PO/$po_fixture.c" "$PO/pointer_output.yaml"
done
check_po_v1_matrix "$PO/pointer_output_variadic_callee.c" "$PO/pointer_output_refusals.yaml"
check_po_v1_matrix "$PO/pointer_output_realloc_name.c" "$PO/pointer_output_refusals.yaml"
# Contract-body-conflict (visible same-TU body, K&R definition): the
# produces effect is never applied; the report is identical to a run
# without the contract entirely.
check_po_v1_matrix "$PO/pointer_output_visible_body.c" "$PO/pointer_output_refusals.yaml"
check_po_v1_matrix "$PO/pointer_output_knr_definition.c" "$PO/pointer_output_refusals.yaml"
# The legacy returns-level nullable key keeps its ignore-semantics for
# non-produces symbols: the bundle loads under the feature.
check_po_converted $PO/pointer_output_returns_nullable_ok.c \
  "--contracts=$PO/pointer_output_returns_nullable_ok.yaml"
# Schema discipline: a T*** parameter type is rejected at application
# time, and every malformed output block is a hard input error.
po_expect_reject "$PO/pointer_output_triple_type.yaml" "$PO/pointer_output_triple_type.c"
po_expect_reject "$PO/pointer_output_missing_output.yaml" "$PO/pointer_output_c1_eq.c"
po_expect_reject "$PO/pointer_output_no_success.yaml" "$PO/pointer_output_c1_eq.c"
po_expect_reject "$PO/pointer_output_output_unknown_key.yaml" "$PO/pointer_output_c1_eq.c"
po_expect_reject "$PO/pointer_output_legacy_nullable.yaml" "$PO/pointer_output_c1_eq.c"
po_expect_reject "$PO/pointer_output_two_out_owner.yaml" "$PO/pointer_output_c1_eq.c"
po_expect_reject "$PO/pointer_output_success_with_always.yaml" "$PO/pointer_output_c1_eq.c"
# v1 fail-closed: produces_out_owner is not in the v1 effect
# vocabulary -- the bundle is refused outright, never degraded.
set +e
"$cand" check --level=cand1 --format=json "$PO/pointer_output_c1_eq.c" \
  --contracts="$PO/pointer_output.yaml" -- -std=c11 >/dev/null 2>&1
status=$?
set -e
[[ "$status" == "2" ]] || { echo "v1 accepted a produces_out_owner bundle"; exit 1; }
# The modifier requires --level=cand1.
set +e
"$cand" check --pointer-output-contracts --format=json "$PO/pointer_output_c1_eq.c" -- -std=c11 >/dev/null 2>&1
status=$?
set -e
[[ "$status" == "2" ]] || { echo "modifier without --level=cand1 was accepted"; exit 1; }
