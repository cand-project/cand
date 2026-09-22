#!/usr/bin/env python3
"""Accumulate fuzz reports into auditable cumulative campaign evidence.

A **unique campaign case** is the triple (generator_sha256, seed, case index
within the seed corpus). Two runs of the same generator with the same seed
produce the same corpus deterministically, so the accumulator counts such a
pair exactly once and records the duplicate separately — duplicate seeds
cannot silently inflate the cumulative total. Verifier or source changes do
not, by themselves, create new unique cases for an unchanged corpus; they are
recorded as separate runs of the same unique-case set.

Every ingested report must carry full provenance (cand_sha256,
generator_sha256, source_commit) and a clean differential gate result;
reports with any false PASS, false positive, coverage gap, wrong failure
class, or harness error fail the accumulation loudly.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REQUIRED_PROVENANCE = ("cand_sha256", "generator_sha256", "source_commit")
GATE_KEYS = ("false_pass", "false_positive", "coverage_gap",
             "wrong_failure_class", "harness_error")


def load_reports(inputs: list[Path]) -> list[tuple[dict, str]]:
    reports: list[tuple[dict, str]] = []
    for path in inputs:
        if path.is_dir():
            reports.extend(load_reports(sorted(p for p in path.iterdir())))
        elif path.name == "report.json":
            reports.append((json.loads(path.read_text(encoding="utf-8")), str(path)))
    return reports


def validate(report: dict, origin: str) -> None:
    if report.get("schema") != "cand.fuzz-report/v2":
        raise SystemExit(f"{origin}: unsupported report schema {report.get('schema')!r}")
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        raise SystemExit(f"{origin}: report predates provenance accounting; regenerate it")
    for field in REQUIRED_PROVENANCE:
        if not provenance.get(field):
            raise SystemExit(f"{origin}: provenance field {field!r} missing or empty")
    results = report.get("results", {})
    for key in GATE_KEYS:
        if results.get(key, 0):
            raise SystemExit(
                f"{origin}: differential gate failed ({key}={results[key]}); "
                "this report cannot contribute to cumulative evidence")
    if not report.get("deterministic_json", False):
        raise SystemExit(f"{origin}: non-deterministic JSON output")
    asan = report.get("asan", {})
    if asan.get("false_pass_cases"):
        raise SystemExit(f"{origin}: ASan-confirmed false PASS cases present")


def accumulate(items: list[tuple[dict, str]]) -> dict:
    seen: dict[tuple, int] = {}
    duplicates: list[dict] = []
    runs = []
    for index, (report, origin) in enumerate(items):
        key = (report["provenance"]["generator_sha256"], report["seed"])
        entry = {
            "seed": report["seed"],
            "cases": report["cases"],
            "cand_sha256": report["provenance"]["cand_sha256"],
            "source_commit": report["provenance"]["source_commit"],
            "origin": origin,
        }
        runs.append(entry)
        if key in seen:
            duplicates.append({"seed": report["seed"], "origin": origin,
                               "counted_from": items[seen[key]][1]})
        else:
            seen[key] = index

    counted_indices = set(seen.values())
    totals = {key: 0 for key in (
        "correct_pass", "correct_fail", "correct_incomplete", "coverage_gap",
        "false_pass", "false_positive", "wrong_failure_class", "harness_error")}
    temporal_confirmations = 0
    for index, (report, _origin) in enumerate(items):
        if index not in counted_indices:
            continue  # duplicate (generator, seed) — counted from its first run
        for name in totals:
            totals[name] += report.get("results", {}).get(name, 0)
        temporal_confirmations += report.get("asan", {}).get("temporal_confirmations", 0)

    unique_cases = sum(runs[index]["cases"] for index in counted_indices)
    return {
        "schema": "cand.fuzz-cumulative/v1",
        "unique_campaign_case_definition":
            "(generator_sha256, seed, case index within the seed corpus)",
        "unique_runs": len(seen),
        "duplicate_runs_excluded": duplicates,
        "unique_cases": unique_cases,
        "verdict_totals": totals,
        "asan_temporal_confirmations": temporal_confirmations,
        "runs": runs,
        "verifier_binaries": sorted({r["cand_sha256"] for r in runs}),
        "source_commits": sorted({r["source_commit"] for r in runs}),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path, nargs="+",
                        help="report.json files or directories containing them")
    parser.add_argument("--output", type=Path, default=None,
                        help="write the cumulative JSON here")
    args = parser.parse_args()

    items = load_reports(args.inputs)
    if not items:
        raise SystemExit("no reports found")
    for report, origin in items:
        validate(report, origin)

    cumulative = accumulate(items)
    text = json.dumps(cumulative, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    print(f"cumulative: {cumulative['unique_cases']} unique cases across "
          f"{cumulative['unique_runs']} unique runs "
          f"({len(cumulative['duplicate_runs_excluded'])} duplicate runs excluded); "
          f"false_pass={cumulative['verdict_totals']['false_pass']}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
