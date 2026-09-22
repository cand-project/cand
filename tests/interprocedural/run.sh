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
