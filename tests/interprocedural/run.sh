#!/usr/bin/env bash
set -euo pipefail
cand="$1"
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
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
echo "interprocedural OK"
