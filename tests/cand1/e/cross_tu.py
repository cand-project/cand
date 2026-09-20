#!/usr/bin/env python3
"""Qualify the deliberately narrow cross-translation-unit boundary."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


P = '#include <stdlib.h>\n#define A(v) __attribute__((annotate(v)))\n#define OWN A("cand:own")\n#define TAKES A("cand:takes")\n#define RET A("cand:returns_own")\n#define BORROW A("cand:returns_borrow_from:0")\n'


CASES = [
    ("safe-no-ownership", "int add(int x) { return x + 1; }\n", "extern int add(int); int main(void) { return add(4); }\n", True),
    ("owned-return", P + "RET int *make(void) { return malloc(sizeof(int)); }\n", P + "extern int *make(void); int main(void) { int *p OWN = make(); free(p); return 0; }\n", False),
    ("destructor", P + "void destroy(int *p) TAKES { free(p); }\n", P + "extern void destroy(int *); int main(void) { int *p OWN = malloc(sizeof(int)); destroy(p); return *p; }\n", False),
    ("borrowed-return", P + "int *view(int *p) BORROW { return p; }\n", P + "extern int *view(int *); int main(void) { int *p OWN = malloc(sizeof(int)); int *v = view(p); free(p); return *v; }\n", False),
    ("mismatched-declaration", P + "RET int *make(void) { return malloc(sizeof(int)); }\n", P + "extern int *make(void); int main(void) { int *p OWN = make(); free(p); return *p; }\n", False),
    ("missing-implementation", P + "extern void absent(int *);\n", P + "extern void absent(int *); int main(void) { int *p OWN = malloc(sizeof(int)); absent(p); free(p); return 0; }\n", False),
]


def policy() -> dict:
    return {
        "schema": "cand.policy/v1", "profile": "generated", "safety_level": "cand1",
        "base_ref": "origin/main",
        "budgets": {"new_unsafe_boundaries": 0, "new_suppressions": 0,
                     "safety_level_reductions": 0, "checked_scope_decrease": 0,
                     "unsupported_scope_increase": 0},
        "unsupported": {"allow_in_verified_success": False},
        "contracts": {"trusted_changes_require_review": True, "trusted": []},
        "scope": {"files": ["a.c", "b.c"]},
        "frontend": {"standard": "c11", "arguments": []},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cand", type=Path)
    args = parser.parse_args()
    outcomes: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="cand1-e-cross-tu-") as name:
        work = Path(name)
        (work / "cand-policy.json").write_text(json.dumps(policy(), indent=2) + "\n")
        for command in (
            ["git", "init", "-q", "-b", "main"],
            ["git", "config", "user.name", "CAND-E"],
            ["git", "config", "user.email", "cand-e@example.invalid"],
        ):
            subprocess.run(command, cwd=work, check=True)
        subprocess.run(["git", "add", "cand-policy.json"], cwd=work, check=True)
        subprocess.run(["git", "commit", "-qm", "cross-TU baseline"], cwd=work, check=True)
        base = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=work, text=True).strip()
        subprocess.run(["git", "update-ref", "refs/remotes/origin/main", base], cwd=work, check=True)
        env = os.environ.copy()
        env["CAND_TRUSTED_BASE_SHA"] = base
        for ident, first, second, safe in CASES:
            (work / "a.c").write_text(first)
            (work / "b.c").write_text(second)
            proc = subprocess.run(
                [str(args.cand.resolve()), "check", "--agent", "--level=cand1", "--format=json",
                 "--base", "origin/main", "--policy", "cand-policy.json", "a.c", "b.c", "--", "-std=c11"],
                cwd=work, env=env, capture_output=True, text=True, timeout=30, check=False,
            )
            try:
                report = json.loads(proc.stdout)
                result = report.get("result", "missing")
                unsupported = len(report.get("analysis", {}).get("unsupported", []))
            except json.JSONDecodeError:
                result, unsupported = "tool-error", -1
            outcomes.append({"id": ident, "safe_control": safe, "result": result, "unsupported": unsupported})
    print(json.dumps({"schema": "cand1-e.cross-tu/v1", "cases": outcomes}, sort_keys=True))
    bad = [x for x in outcomes if not x["safe_control"] and x["result"] == "pass"]
    safe_bad = [x for x in outcomes if x["safe_control"] and x["result"] not in {"pass", "incomplete"}]
    if bad or safe_bad:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
