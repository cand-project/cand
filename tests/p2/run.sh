#!/usr/bin/env bash
set -euo pipefail

cand="${1:?path to cand binary required}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

check_result() {
    local expected="$1" expected_id="$2" source="$3"
    local output rc
    set +e
    output="$($cand check --format=json "$source" -- -std=c11 -Iinclude 2>/dev/null)"
    rc=$?
    set -e
    python3 - "$expected" "$expected_id" "$rc" "$source" "$output" <<'PY'
import json
import sys

expected, expected_id, rc, source, text = sys.argv[1:]
data = json.loads(text)
if data.get("result") != expected:
    raise SystemExit(f"{source}: expected {expected}, got {data.get('result')}: {data}")
if expected == "fail":
    ids = [item.get("id") for item in data.get("findings", [])]
    if expected_id not in ids:
        raise SystemExit(f"{source}: missing {expected_id}, got {ids}")
    if int(rc) != 1:
        raise SystemExit(f"{source}: semantic failure returned {rc}")
elif expected == "incomplete" and int(rc) != 3:
    raise SystemExit(f"{source}: incomplete result returned {rc}")
elif expected == "pass":
    if data.get("findings") or data.get("unsupported"):
        raise SystemExit(f"{source}: unexpected diagnostics: {data}")
    if int(rc) != 0:
        raise SystemExit(f"{source}: pass returned {rc}")
PY
    echo "P2 OK ($expected): $source"
}

# Shared borrowed returns and their safe uses.
check_result pass "" tests/p2/shared_borrowed_return.c
check_result pass "" tests/p2/move_preserves_borrow.c
check_result pass "" tests/p2/last_use_then_destroy.c
check_result pass "" tests/p2/wrapper_borrowed_return.c
check_result pass "" tests/p2/borrowed_field_return.c
check_result pass "" tests/p2/cfg_branch_last_use.c
check_result pass "" tests/p2/cfg_switch_exclusive.c
check_result pass "" tests/p2/cfg_early_return.c
check_result pass "" tests/p2/cfg_goto_exclusive.c
check_result pass "" tests/p2/cfg_loop_last_use.c

# Lifetime and exclusivity failures.
check_result fail CAND-B001 tests/p2/destroy_with_live_borrow.c
check_result fail CAND-B002 tests/p2/use_after_parent_death.c
check_result fail CAND-B002 tests/p2/cfg_loop_after_death.c
check_result fail CAND-B002 tests/p2/cfg_branch_after_death.c
check_result fail CAND-B003 tests/p2/direct_global_escape.c
check_result fail CAND-B003 tests/p2/undeclared_borrow_return.c
check_result fail CAND-B004 tests/p2/mutable_conflict.c

# These boundaries are deliberately fail-closed until retention/escape
# summaries are available.
check_result incomplete "" tests/p2/unknown_call_retention.c
check_result incomplete "" tests/p2/global_escape.c
check_result incomplete "" tests/p2/indirect_call_retention.c
check_result fail CAND-B001 tests/p2/mutable_owner_access.c
check_result fail CAND-T002 tests/p2/aggregate_transport.c
check_result fail CAND-T002 tests/p2/array_transport.c
check_result incomplete "" tests/p2/memcpy_transport.c
check_result incomplete "" tests/p2/cast_transport.c
check_result incomplete "" tests/p2/cast_live.c
check_result incomplete "" tests/p2/union_transport.c
check_result incomplete "" tests/p2/realloc_borrow.c

for source in tests/p2/*.c; do
    cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
    clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only "$source"
    clang -std=c11 -Wall -Wextra -Werror -DCAND_ANALYSIS=1 -Iinclude -fsyntax-only "$source"
done

python3 tests/p2/adversarial.py "$cand"

set +e
natural_output="$($cand check --format=json tests/interprocedural/natural_router.c -- -std=c11 -Iinclude 2>/dev/null)"
natural_rc=$?
set -e
if [[ "$natural_rc" != 3 ]]; then
    echo "natural_router.c: expected exit 3, got $natural_rc" >&2
    exit 1
fi
python3 - "$natural_output" <<'PY'
import json
import sys

report = json.loads(sys.argv[1])
analysis = report.get("borrow_analysis", {})
if report.get("result") != "incomplete":
    raise SystemExit(f"natural_router.c: expected INCOMPLETE, got {report.get('result')}")
if analysis.get("borrows_created", 0) < 1:
    raise SystemExit(f"natural_router.c: no borrow relations recorded: {analysis}")
if analysis.get("unsupported_borrow_operations") != 0:
    raise SystemExit(f"natural_router.c: unsupported borrow operation: {analysis}")
print("P2 natural borrowed-view demo: INCOMPLETE with modeled borrows and zero unsupported borrow operations")
PY

echo "C& P2 borrow/lifetime corpus passed."
