#!/usr/bin/env python3
"""Normative, deterministic C&1-C conformance fixtures."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import Counter
from pathlib import Path
import sys

FUZZ = Path(__file__).parents[2] / "fuzz"
sys.path.insert(0, str(FUZZ))
from taxonomy import classify, make_case, render  # noqa: E402

ROOT = Path(__file__).parents[3]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cand", type=Path)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("manifest.json"))
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    counts = Counter()
    with tempfile.TemporaryDirectory(prefix="cand1-conformance-") as name:
        directory = Path(name)
        for entry in manifest["fixtures"]:
            case = make_case(
                0, entry["case_index"], entry["class"], entry["template"],
                tuple(entry["mechanisms"]),
            )
            source = directory / f"{entry['id']}.c"
            source.write_text(render(case), encoding="utf-8")
            proc = subprocess.run(
                [str(args.cand), "check", "--level=cand1", "--format=json", str(source), "--",
                 "-std=c11", f"-I{ROOT / 'include'}"],
                cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
            )
            try:
                report = json.loads(proc.stdout)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{entry['id']}: invalid cand JSON") from exc
            bucket = classify(case, report, proc.returncode)
            counts[bucket] += 1
            if bucket in {"FALSE_PASS", "FALSE_POSITIVE", "HARNESS_ERROR", "WRONG_FAILURE_CLASS"}:
                raise SystemExit(f"{entry['id']}: {bucket}: {report}")
    result = {"schema": "cand.conformance-report/v1", "fixtures": len(manifest["fixtures"]),
              "results": dict(sorted(counts.items()))}
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
