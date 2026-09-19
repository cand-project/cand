#!/usr/bin/env python3
"""Normative C&1 fixtures, each checked through the strict PASS path."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
import sys

FUZZ = Path(__file__).parents[2] / "fuzz"
sys.path.insert(0, str(FUZZ))
from strict import StrictWorkspace  # noqa: E402
from taxonomy import classify, detect_mechanisms, make_case, render, validate_source  # noqa: E402

ROOT = Path(__file__).parents[3]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cand", type=Path)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("manifest.json"))
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    counts = Counter()
    results = []
    with StrictWorkspace(args.cand) as strict:
        for entry in manifest["fixtures"]:
            case = make_case(0, entry["case_index"], entry["class"], entry["template"],
                             tuple(entry["mechanisms"]))
            source = render(case)
            validate_source(case, source)
            if set(detect_mechanisms(source)) != set(entry["mechanisms"]):
                raise SystemExit(f"{entry['id']}: manifest/source mechanism mismatch")
            report, returncode, _stdout, _stderr = strict.run(source)
            bucket = classify(case, report, returncode)
            counts[bucket] += 1
            results.append({"id": entry["id"], "class": entry["class"],
                            "mechanisms": entry["mechanisms"], "cand": report.get("result"),
                            "qualification": bucket})
            if bucket not in {"CORRECT_PASS", "CORRECT_FAIL", "CORRECT_INCOMPLETE"}:
                raise SystemExit(f"{entry['id']}: {bucket}: {report}")
    result = {"schema": "cand.conformance-report/v2", "fixtures": len(results),
              "results": dict(sorted(counts.items())), "cases": results}
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
