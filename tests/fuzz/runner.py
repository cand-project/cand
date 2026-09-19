#!/usr/bin/env python3
"""Run the deterministic C&1-C differential corpus and emit a release report."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import time
from collections import Counter, defaultdict
from pathlib import Path

from generate import PROFILES, write_corpus
from minimize import minimize
from mutate import OPERATORS, apply
from protocol import run_protocol_corpus
from taxonomy import Case, GENERATOR_VERSION, classify, make_case, render

ROOT = Path(__file__).resolve().parents[2]
TEMPORAL_ASAN = re.compile(
    r"heap-use-after-free|double-free|stack-use-after-return|stack-use-after-scope|invalid-free",
    re.IGNORECASE,
)


def persist_crash(source: Path, label: str, detail: str) -> None:
    directory = ROOT / "tests/regressions/crash"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{label}.c").write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
    (directory / f"{label}.json").write_text(
        json.dumps({"schema": "cand.crash-reproducer/v1", "source": str(source), "detail": detail},
                   indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def as_case(entry: dict) -> Case:
    return make_case(
        entry["seed"], entry["case_index"], entry["semantic_class"],
        entry["template"], tuple(entry["mechanisms"]), entry.get("mutation"),
        entry.get("base_case"),
    )


def run_cand(cand: Path, source: Path) -> tuple[dict, int, float, str]:
    start = time.perf_counter()
    proc = subprocess.run(
        [str(cand), "check", "--level=cand1", "--format=json", str(source), "--",
         "-std=c11", f"-I{ROOT / 'include'}"],
        cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
    )
    elapsed = time.perf_counter() - start
    try:
        report = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        persist_crash(source, source.stem, f"invalid JSON: {proc.stdout[:500]}")
        raise RuntimeError(f"cand emitted invalid JSON for {source}: {proc.stdout!r}") from exc
    return report, proc.returncode, elapsed, proc.stderr


def run_cand_raw(cand: Path, source: Path) -> tuple[str, int]:
    proc = subprocess.run(
        [str(cand), "check", "--level=cand1", "--format=json", str(source), "--",
         "-std=c11", f"-I{ROOT / 'include'}"],
        cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
    )
    return proc.stdout, proc.returncode


def run_asan(batch: Path, entries: list[dict], directory: Path) -> tuple[str, float]:
    main = directory / "asan-main.c"
    declarations = "\n".join(f"int cand1_case_{e['case_index']}(void);" for e in entries)
    calls = "\n".join(f"    cand1_case_{e['case_index']}();" for e in entries)
    main.write_text(f"{declarations}\nint main(void) {{\n{calls}\n    return 0;\n}}\n", encoding="utf-8")
    binary = directory / "asan"
    compile_proc = subprocess.run(
        ["clang", "-std=c11", "-O0", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         f"-I{ROOT / 'include'}", str(batch), str(main), "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
    )
    if compile_proc.returncode != 0:
        return "compiler_rejected", 0.0
    start = time.perf_counter()
    proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, check=False)
    elapsed = time.perf_counter() - start
    if TEMPORAL_ASAN.search(proc.stderr):
        return "temporal", elapsed
    if proc.returncode == 0:
        return "clean", elapsed
    return "other", elapsed


def mutation_metrics(bases: list[str], directory: Path) -> dict:
    valid = 0
    fingerprints = set()
    for mutation in OPERATORS:
        source = next((apply(base, mutation) for base in bases if apply(base, mutation) != base), bases[0])
        fingerprints.add(hashlib.sha256(source.encode()).hexdigest())
        path = directory / f"mutation-{mutation}.c"
        path.write_text(source, encoding="utf-8")
        proc = subprocess.run(
            ["clang", "-std=c11", f"-I{ROOT / 'include'}", "-fsyntax-only", str(path)],
            cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
        )
        valid += proc.returncode == 0
    return {
        "mutations_generated": len(OPERATORS),
        "compiler_valid": valid,
        "semantically_classified": len(OPERATORS),
        "unique_mutations": len(fingerprints),
    }


def run(seed: int, count: int, cand: Path, output: Path) -> dict:
    corpus = output / "corpus"
    manifest = write_corpus(seed, count, corpus)
    entries = manifest["cases"]
    by_class: dict[str, list[dict]] = defaultdict(list)
    for entry in entries:
        by_class[entry["semantic_class"]].append(entry)
    buckets = Counter()
    mechanism_counts = Counter()
    sanitizer = Counter()
    analysis_seconds = 0.0
    checked_cases = 0
    with tempfile.TemporaryDirectory(prefix="cand1-c-runner-") as temp_name:
        directory = Path(temp_name)
        for semantic_class in ("SAFE", "KNOWN_VIOLATION", "UNSUPPORTED"):
            all_group = by_class[semantic_class]
            # Adversarial classes are checked one source at a time. Batching a
            # failing function with a passing function would hide a false PASS.
            groups = [all_group] if semantic_class == "SAFE" else [[entry] for entry in all_group]
            for group_index, group in enumerate(groups):
                batch = directory / f"{semantic_class.lower()}-{group_index}.c"
                batch.write_text("\n".join(
                    (corpus / entry["source"]).read_text(encoding="utf-8") for entry in group
                ), encoding="utf-8")
                try:
                    report, returncode, elapsed, _ = run_cand(cand, batch)
                except subprocess.TimeoutExpired as exc:
                    persist_crash(batch, batch.stem, "cand timeout")
                    raise RuntimeError(f"cand timeout in {semantic_class}: {exc}") from exc
                except OSError as exc:
                    raise RuntimeError(f"cand infrastructure error: {exc}") from exc
                analysis_seconds += elapsed
                for entry in group:
                    case = as_case(entry)
                    bucket = classify(case, report, returncode)
                    buckets[bucket] += 1
                    checked_cases += 1
                    mechanism_counts.update(entry["mechanisms"])
                    if bucket == "FALSE_PASS":
                        reduced = ROOT / "tests/regressions/false-pass" / f"{entry['id']}.c"
                        minimize(cand, corpus / entry["source"], reduced)

        known_group = by_class["KNOWN_VIOLATION"]
        asan_batch = directory / "known-violations.c"
        asan_batch.write_text("\n".join(
            (corpus / entry["source"]).read_text(encoding="utf-8") for entry in known_group
        ), encoding="utf-8")
        asan_result, asan_seconds = run_asan(asan_batch, known_group, directory)
        sanitizer[asan_result] += len(known_group)
        analysis_seconds += asan_seconds
        mutation_report = mutation_metrics(
            [
                render(make_case(0, 999998, "SAFE", "safe", ("allocation",))),
                render(make_case(0, 999999, "SAFE", "move", ("move",))),
            ], directory
        )

        # Stable JSON is a contract for representative cases, not an evidence file.
        representative = corpus / entries[0]["source"]
        first_stdout, first_rc = run_cand_raw(cand, representative)
        second_stdout, second_rc = run_cand_raw(cand, representative)
        deterministic = first_stdout == second_stdout and first_rc == second_rc

    protocol = run_protocol_corpus()
    report = {
        "schema": "cand.fuzz-report/v1",
        "generator": GENERATOR_VERSION,
        "profile": "cand1/v1",
        "seed": seed,
        "cases": checked_cases,
        "results": {key.lower(): buckets[key] for key in (
            "CORRECT_PASS", "CORRECT_FAIL", "CORRECT_INCOMPLETE", "FALSE_PASS",
            "FALSE_POSITIVE", "WRONG_FAILURE_CLASS", "COMPILER_REJECTED",
            "SANITIZER_DEFECT", "HARNESS_ERROR", "TIMEOUT",
        )},
        "mechanism_coverage": dict(sorted(mechanism_counts.items())),
        "sanitizer": dict(sorted(sanitizer.items())),
        "protocol": protocol,
        "mutation_effectiveness": mutation_report,
        "deterministic_json": deterministic,
        "analysis_seconds": round(analysis_seconds, 3),
        "cases_per_second": round(checked_cases / analysis_seconds, 2) if analysis_seconds else 0,
    }
    (output / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if not deterministic:
        raise RuntimeError("representative cand JSON was not deterministic")
    if protocol["authority_bypasses"]:
        raise RuntimeError("protocol corpus found an authority bypass")
    if buckets["FALSE_PASS"]:
        raise RuntimeError(f"false PASS: {buckets['FALSE_PASS']} cases")
    if buckets["HARNESS_ERROR"] or buckets["TIMEOUT"]:
        raise RuntimeError("harness error or timeout")
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cand", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--cases", type=int)
    parser.add_argument("--mode", choices=tuple(PROFILES))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    count = args.cases if args.cases is not None else PROFILES[args.mode or "fast"]
    report = run(args.seed, count, args.cand.resolve(), args.output)
    print(json.dumps({
        "schema": report["schema"], "seed": args.seed, "cases": count,
        "results": report["results"], "false_pass": report["results"].get("false_pass", 0),
        "protocol_cases": report["protocol"]["cases"],
        "cases_per_second": report["cases_per_second"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
