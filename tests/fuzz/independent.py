#!/usr/bin/env python3
"""Fresh, individually analyzed exact-head cases with a separate renderer."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from strict import StrictWorkspace  # noqa: E402
from taxonomy import (  # noqa: E402
    classify, common_source, detect_mechanisms, features_for, make_case, validate_source,
)

ROOT = Path(__file__).resolve().parents[2]
SEED = 0xC01D


def independent_source(index: int, semantic_class: str, template: str) -> str:
    n, item = index + 700000, f"CandItem{index + 700000}"
    prefix = common_source(n)
    if semantic_class == "SAFE":
        body = f"""int independent_case_{index}(void) {{
    {item} *owner CAND_OWN = malloc(sizeof *owner);
    if (!owner) return 0;
    owner->value = {index};
    volatile int cand1_sink{n} = 0;
    if (owner->value >= 0) cand1_sink{n} = owner->value;
    free(owner);
    return cand1_sink{n};
}}
"""
    elif semantic_class == "KNOWN_VIOLATION":
        body = f"""int independent_case_{index}(void) {{
    {item} *owner CAND_OWN = malloc(sizeof *owner);
    if (!owner) return 0;
    volatile int cand1_sink{n} = 0;
    free(owner);
    cand1_sink{n} = owner->value;
    return cand1_sink{n};
}}
"""
    else:
        operation = {
            "u-memcpy": f"    {item} *copy = NULL;\n    memcpy(&copy, &owner, sizeof copy);\n",
            "u-struct-field": f"    struct Holder_{n} {{ {item} *p; }} holder;\n    holder.p = owner;\n    memcpy(&owner, &holder.p, sizeof owner);\n",
            "u-array": f"    {item} *items_{n}[1] = {{ owner }};\n    free(items_{n}[0]);\n",
            "u-aggregate": f"    struct Holder_{n} {{ {item} *p; }} holder = {{ .p = owner }};\n    free(holder.p);\n",
            "u-union": f"    union Union_{n} {{ {item} *p; void *opaque; }} value = {{ .p = owner }};\n    free(value.p);\n",
        }[template]
        body = f"""int independent_case_{index}(void) {{
    {item} *owner CAND_OWN = malloc(sizeof *owner);
    if (!owner) return 0;
    owner->value = {index};
{operation}    free(owner);
    return 0;
}}
"""
    return prefix + body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cand", type=Path, required=True)
    args = parser.parse_args()
    cases = []
    for index in range(100):
        group = index // 25
        if group == 0:
            cls, template = "SAFE", "safe-branch"
        elif group == 1:
            cls, template = "KNOWN_VIOLATION", "violation"
        elif group == 2:
            cls, template = "UNSUPPORTED", ("u-memcpy", "u-struct-field", "u-array",
                                             "u-aggregate", "u-union")[index % 5]
        else:
            cls, template = ("SAFE", "safe") if index % 3 == 0 else (
                ("KNOWN_VIOLATION", "violation") if index % 3 == 1 else (
                    "UNSUPPORTED", ("u-memcpy", "u-struct-field", "u-array",
                                     "u-aggregate", "u-union")[index % 5]))
        cases.append((index, cls, template))

    counts = Counter()
    mechanisms = set()
    results = []
    with StrictWorkspace(args.cand) as strict:
        for index, cls, template in cases:
            case = make_case(SEED, index + 700000, cls, template)
            source = independent_source(index, cls, template)
            validate_source(case, source)
            if set(detect_mechanisms(source)) != set(case.mechanisms):
                raise SystemExit(f"independent-{index:03d}: source mechanism mismatch")
            mechanisms.update(features_for(template))
            report, returncode, _stdout, _stderr = strict.run(source)
            bucket = classify(case, report, returncode)
            counts[bucket] += 1
            results.append({"id": f"independent-{index:03d}", "cand": report.get("result"),
                            "qualification": bucket})
            if bucket not in {"CORRECT_PASS", "CORRECT_FAIL", "CORRECT_INCOMPLETE"}:
                raise SystemExit(f"independent-{index:03d}: {bucket}: {report}")
    print(json.dumps({"schema": "cand.independent-report/v2", "cases": len(results),
                      "seed": SEED, "mechanism_diversity": sorted(mechanisms),
                      "results": dict(sorted(counts.items())), "case_results": results}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
