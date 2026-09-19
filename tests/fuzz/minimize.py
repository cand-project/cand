#!/usr/bin/env python3
"""Small line-deletion reducer for a confirmed cand1 false PASS."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def is_false_pass(cand: Path, source: Path) -> bool:
    proc = subprocess.run(
        [str(cand), "check", "--level=cand1", "--format=json", str(source), "--",
         "-std=c11", f"-I{ROOT / 'include'}"],
        cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
    )
    try:
        return proc.returncode == 0 and json.loads(proc.stdout).get("result") == "pass"
    except json.JSONDecodeError:
        return False


def minimize(cand: Path, source: Path, output: Path) -> Path:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    with tempfile.TemporaryDirectory(prefix="cand1-minimize-") as name:
        candidate = Path(name) / "candidate.c"
        changed = True
        while changed:
            changed = False
            for index in range(len(lines)):
                trial = lines[:index] + lines[index + 1:]
                candidate.write_text("".join(trial), encoding="utf-8")
                if is_false_pass(cand, candidate):
                    lines = trial
                    changed = True
                    break
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(lines), encoding="utf-8")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cand", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    path = minimize(args.cand.resolve(), args.source.resolve(), args.output)
    print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
