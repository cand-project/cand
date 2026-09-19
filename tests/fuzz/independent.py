#!/usr/bin/env python3
"""Independent exact-head smoke corpus: 100 fresh cases, separate from seeds."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from taxonomy import classify, make_case, render  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cand", type=Path, required=True)
    args = parser.parse_args()
    groups = defaultdict(list)
    for index in range(100):
        cls = ("SAFE", "KNOWN_VIOLATION", "UNSUPPORTED")[index % 3]
        template = {"SAFE": "safe", "KNOWN_VIOLATION": "violation", "UNSUPPORTED": "unsupported"}[cls]
        case = make_case(0xC01D, index, cls, template, ("independent", "transport"))
        groups[cls].append(case)
    with tempfile.TemporaryDirectory(prefix="cand1-independent-") as name:
        directory = Path(name)
        for cls, cases in groups.items():
            source = directory / f"{cls.lower()}.c"
            source.write_text("\n".join(render(case) for case in cases), encoding="utf-8")
            proc = subprocess.run(
                [str(args.cand), "check", "--level=cand1", "--format=json", str(source), "--",
                 "-std=c11", f"-I{ROOT / 'include'}"],
                cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
            )
            report = json.loads(proc.stdout)
            bucket = classify(cases[0], report, proc.returncode)
            if bucket in {"FALSE_PASS", "HARNESS_ERROR", "WRONG_FAILURE_CLASS"}:
                raise SystemExit(f"independent {cls}: {bucket}: {report}")
    print(json.dumps({"cases": 100, "seeds": [0xC01D], "false_pass": 0}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
