#!/usr/bin/env python3
"""Independent C&1-E adversarial corpus.

The cases deliberately use different declaration, expression, block, wrapper,
and control-flow shapes from the implementation-owned fixtures. Negative cases
must never produce authoritative cand1 PASS. Safe controls are reported
separately so conservative INCOMPLETE remains visible instead of being hidden.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


PREFIX = r'''
#include <stdlib.h>
#include <stdint.h>
#define CAND_A(v) __attribute__((annotate(v)))
#define CAND_OWN CAND_A("cand:own")
#define CAND_BORROW CAND_A("cand:borrow_shared")
#define CAND_BORROW_MUT CAND_A("cand:borrow_mut")
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_RETURNS_OWN CAND_A("cand:returns_own")
#define CAND_RETURNS_BORROW_FROM(n) CAND_A("cand:returns_borrow_from:" #n)
#define CAND_MOVE(x) (x)
'''


def case(name: str, negative: bool, body: str) -> tuple[str, bool, str]:
    return name, negative, PREFIX + "\n" + body + "\n"


def build_cases() -> list[tuple[str, bool, str]]:
    cases: list[tuple[str, bool, str]] = []
    for i in range(12):
        cases.append(case(f"safe-owner-wrapper-{i}", False, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
static int read_value(const int *p) {{ return *p + {i}; }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    int result = read_value(owner);
    free(owner);
    return result;
}}
'''))
        cases.append(case(f"bad-owner-after-release-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
static int read_value(const int *p) {{ return *p + {i}; }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    int *alias = owner;
    if (({i} & 1) != 0) {{ free(alias); }} else {{ free(owner); }}
    return read_value(owner);
}}
'''))

    for i in range(10):
        cases.append(case(f"safe-move-join-{i}", False, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
int main(void) {{
    int *source CAND_OWN = obtain();
    int *target CAND_OWN = CAND_MOVE(source);
    if ({i} & 1) {{ target = target; }}
    int value = *target;
    free(target);
    return value;
}}
'''))
        cases.append(case(f"bad-moved-source-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
int main(void) {{
    int *source CAND_OWN = obtain();
    int *target CAND_OWN = CAND_MOVE(source);
    int value = *source + *target + {i};
    free(target);
    return value;
}}
'''))

    for i in range(10):
        cases.append(case(f"safe-borrow-last-use-{i}", False, f'''
typedef struct Cell {{ int value; }} Cell;
CAND_RETURNS_OWN Cell *obtain(void) {{ return malloc(sizeof(Cell)); }}
CAND_RETURNS_BORROW_FROM(0) int *view_mut(Cell *p) {{ return &p->value; }}
int main(void) {{
    Cell *owner CAND_OWN = obtain();
    int *first CAND_BORROW_MUT = view_mut(owner);
    int old = *first + {i};
    int *second CAND_BORROW_MUT = view_mut(owner);
    *second = old;
    free(owner);
    return old;
}}
'''))
        cases.append(case(f"bad-borrow-overlap-{i}", True, f'''
typedef struct Cell {{ int value; }} Cell;
CAND_RETURNS_OWN Cell *obtain(void) {{ return malloc(sizeof(Cell)); }}
CAND_RETURNS_BORROW_FROM(0) int *view_mut(Cell *p) {{ return &p->value; }}
int main(void) {{
    Cell *owner CAND_OWN = obtain();
    int *first CAND_BORROW_MUT = view_mut(owner);
    int *second CAND_BORROW_MUT = view_mut(owner);
    *first += *second + {i};
    free(owner);
    return *first;
}}
'''))

    for i in range(8):
        cases.append(case(f"bad-borrow-parent-join-{i}", True, f'''
typedef struct Cell {{ int value; }} Cell;
CAND_RETURNS_OWN Cell *obtain(void) {{ return malloc(sizeof(Cell)); }}
CAND_RETURNS_BORROW_FROM(0) int *view(Cell *p) {{ return &p->value; }}
int main(void) {{
    Cell *owner CAND_OWN = obtain();
    int *borrow CAND_BORROW = view(owner);
    if ({i} & 1) {{ free(owner); }} else {{ free(owner); }}
    return *borrow;
}}
'''))

    for i in range(8):
        cases.append(case(f"bad-aggregate-stale-{i}", True, f'''
typedef struct Slot {{ int *payload; }} Slot;
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    Slot first = {{ .payload = owner }};
    Slot second = first;
    free(owner);
    return *second.payload + {i};
}}
'''))
        cases.append(case(f"bad-array-stale-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(3 * sizeof(int)); }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    int *items[2] = {{ owner, owner }};
    free(items[{i} % 2]);
    return *items[{(i + 1) % 2}];
}}
'''))

    for i in range(8):
        cases.append(case(f"bad-loop-generation-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
int main(void) {{
    int *remembered = 0;
    for (int round = 0; round < 2; ++round) {{
        int *current CAND_OWN = obtain();
        if (round == 0) remembered = current;
        free(current);
    }}
    return *remembered + {i};
}}
'''))
        cases.append(case(f"bad-loop-conditional-destroy-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    for (int round = 0; round < 2; ++round) {{
        if ((round + {i}) & 1) free(owner);
    }}
    return *owner;
}}
'''))

    for i in range(6):
        cases.append(case(f"bad-wrapper-destroy-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
void consume(int *p) CAND_TAKES {{ free(p); }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    consume(owner);
    return *owner + {i};
}}
'''))
        cases.append(case(f"bad-unknown-retention-{i}", True, f'''
typedef struct Cell {{ int value; }} Cell;
extern void retain(Cell *);
CAND_RETURNS_OWN Cell *obtain(void) {{ return malloc(sizeof(Cell)); }}
int main(void) {{
    Cell *owner CAND_OWN = obtain();
    retain(owner);
    free(owner);
    return owner->value + {i};
}}
'''))

    for i in range(6):
        cases.append(case(f"unsupported-pointer-integer-{i}", True, f'''
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
int main(void) {{
    int *owner CAND_OWN = obtain();
    uintptr_t bits = (uintptr_t)owner + {i};
    int *roundtrip = (int *)bits;
    free(owner);
    return *roundtrip;
}}
'''))
        cases.append(case(f"unsupported-nonlocal-{i}", True, f'''
#include <setjmp.h>
CAND_RETURNS_OWN int *obtain(void) {{ return malloc(sizeof(int)); }}
static jmp_buf escape;
int main(void) {{
    int *owner CAND_OWN = obtain();
    if (setjmp(escape) == 0) {{ free(owner); longjmp(escape, 1); }}
    return *owner + {i};
}}
'''))

    return cases


def init_workspace(path: Path) -> str:
    policy = {
        "schema": "cand.policy/v1", "profile": "generated", "safety_level": "cand1",
        "base_ref": "origin/main",
        "budgets": {"new_unsafe_boundaries": 0, "new_suppressions": 0,
                     "safety_level_reductions": 0, "checked_scope_decrease": 0,
                     "unsupported_scope_increase": 0},
        "unsupported": {"allow_in_verified_success": False},
        "contracts": {"trusted_changes_require_review": True, "trusted": []},
        "scope": {"files": ["case.c"]}, "frontend": {"standard": "c11", "arguments": []},
    }
    (path / "cand-policy.json").write_text(json.dumps(policy, indent=2) + "\n")
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=path, check=True)
    subprocess.run(["git", "config", "user.name", "CAND-E"], cwd=path, check=True)
    subprocess.run(["git", "config", "user.email", "cand-e@example.invalid"], cwd=path, check=True)
    subprocess.run(["git", "add", "cand-policy.json"], cwd=path, check=True)
    subprocess.run(["git", "commit", "-qm", "independent qualification baseline"], cwd=path, check=True)
    base = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=path, text=True).strip()
    subprocess.run(["git", "update-ref", "refs/remotes/origin/main", base], cwd=path, check=True)
    return base


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cand", type=Path)
    args = parser.parse_args()
    cases = build_cases()
    negative_passes: list[str] = []
    tool_errors: list[str] = []
    safe_failures: list[tuple[str, str]] = []
    results: dict[str, int] = {}
    with tempfile.TemporaryDirectory(prefix="cand1-e-independent-") as name:
        work = Path(name)
        base = init_workspace(work)
        env = os.environ.copy()
        env["CAND_TRUSTED_BASE_SHA"] = base
        for ident, negative, source in cases:
            (work / "case.c").write_text(source)
            proc = subprocess.run(
                [str(args.cand.resolve()), "check", "--agent", "--level=cand1",
                 "--format=json", "--base", "origin/main", "--policy", "cand-policy.json",
                 "case.c", "--", "-std=c11"],
                cwd=work, env=env, capture_output=True, text=True, timeout=30, check=False,
            )
            try:
                report = json.loads(proc.stdout)
            except json.JSONDecodeError:
                tool_errors.append(ident + ": malformed authoritative output")
                continue
            result = report.get("result", "missing")
            results[result] = results.get(result, 0) + 1
            if negative and result == "pass":
                negative_passes.append(ident)
            if not negative and result not in {"pass", "incomplete"}:
                safe_failures.append((ident, result))
    print(json.dumps({
        "schema": "cand1-e.independent/v1", "cases": len(cases),
        "negative_cases": sum(negative for _, negative, _ in cases),
        "safe_controls": sum(not negative for _, negative, _ in cases),
        "results": dict(sorted(results.items())),
        "negative_false_passes": negative_passes,
        "tool_errors": tool_errors,
        "safe_control_failures": safe_failures,
        "base_policy": "generated/cand1, zero weakening budgets",
    }, sort_keys=True))
    if negative_passes:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
