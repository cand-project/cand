#!/usr/bin/env python3
"""Run source-driven C&1-C qualification with one strict verdict per case."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import re
import subprocess
import tempfile
import threading
import time
from collections import Counter, defaultdict
from pathlib import Path

from generate import PROFILES, write_corpus
from minimize import minimize
from mutate import OPERATORS, apply, validate_mutation
from protocol import run_protocol_corpus
from strict import EXTERN_HELPER_C, StrictWorkspace
from taxonomy import (
    GENERATOR_VERSION, Case, classify, detect_mechanisms, make_case, render, validate_source,
)

ROOT = Path(__file__).resolve().parents[2]


def _provenance(cand: Path) -> dict:
    """Machine-readable run provenance for cumulative campaign accounting.

    A unique campaign case is defined as (generator_sha256, seed, case index
    within the seed corpus); re-running an identical (generator, seed) pair
    cannot inflate the cumulative total (accumulate.py deduplicates on that
    key and counts duplicates separately).
    """
    import hashlib

    def digest(data: bytes) -> str:
        return hashlib.sha256(data).hexdigest()

    py_files = sorted(p for p in (ROOT / "tests" / "fuzz").glob("*.py"))
    provenance = {
        "cand_sha256": digest(cand.read_bytes()),
        # The generator identity is the fuzz-harness source itself; re-running
        # the same generator+seed pair cannot add unique campaign cases.
        "generator_sha256": digest(b"".join(p.read_bytes() for p in py_files)),
    }
    try:
        provenance["source_commit"] = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        provenance["source_commit"] = "unknown"
    return provenance


TEMPORAL_PATTERNS = (
    ("heap_use_after_free", re.compile(r"heap-use-after-free", re.IGNORECASE)),
    ("double_free", re.compile(r"double-free", re.IGNORECASE)),
    ("invalid_free", re.compile(r"invalid-free", re.IGNORECASE)),
    ("stack_use_after_return", re.compile(r"stack-use-after-return", re.IGNORECASE)),
    ("stack_use_after_scope", re.compile(r"stack-use-after-scope", re.IGNORECASE)),
)
_WORKER_STATE = threading.local()
_WORKER_WORKSPACES: list[StrictWorkspace] = []
_WORKER_LOCK = threading.Lock()


def as_case(entry: dict) -> Case:
    return make_case(entry["seed"], entry["case_index"], entry["semantic_class"], entry["template"],
                     tuple(entry["mechanisms"]), entry.get("mutation"), entry.get("base_case"))


def _worker_workspace(cand: Path) -> StrictWorkspace:
    strict = getattr(_WORKER_STATE, "strict", None)
    if strict is None:
        strict = StrictWorkspace(cand)
        _WORKER_STATE.strict = strict
        with _WORKER_LOCK:
            _WORKER_WORKSPACES.append(strict)
    return strict


def _analyze_entry(cand: Path, corpus: Path, entry: dict) -> tuple[dict, int, str, tuple[str, ...], str]:
    case = as_case(entry)
    source = (corpus / entry["source"]).read_text(encoding="utf-8")
    validate_source(case, source)
    actual = detect_mechanisms(source)
    if set(actual) != set(entry["mechanisms"]):
        raise RuntimeError(f"{entry['id']}: manifest/source mechanism mismatch")
    report, returncode, stdout, stderr = _worker_workspace(cand).run(source)
    return report, returncode, stdout, actual, stderr


def _run_asan_cases(entries: list[dict], corpus: Path, directory: Path) -> tuple[list[dict], float]:
    """Compile once, execute each case ID in a separate process."""
    batch = directory / "known-violations.c"
    sources = [((corpus / entry["source"]).read_text(encoding="utf-8")) for entry in entries]
    batch.write_text("\n".join(sources), encoding="utf-8")
    main = directory / "asan-main.c"
    declarations = "\n".join(f"int cand1_case_{e['case_index']}(void);" for e in entries)
    dispatch = "\n".join(f"        case {e['case_index']}: return cand1_case_{e['case_index']}();" for e in entries)
    main.write_text(
        f"#include <stdlib.h>\n{declarations}\nint main(int argc, char **argv) {{\n"
        f"    if (argc != 2) return 2;\n    switch (atoi(argv[1])) {{\n{dispatch}\n"
        "        default: return 2;\n    }\n}\n", encoding="utf-8"
    )
    binary = directory / "asan"
    compile_proc = subprocess.run(
        ["clang", "-std=c11", "-O0", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(batch), str(main), "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
    )
    if compile_proc.returncode != 0:
        return [{"id": e["id"], "sanitizer": "compiler_rejected", "returncode": compile_proc.returncode}
                for e in entries], 0.0
    results = []
    start = time.perf_counter()
    for entry in entries:
        try:
            proc = subprocess.run([str(binary), str(entry["case_index"])], capture_output=True, text=True,
                                  timeout=30, check=False)
        except subprocess.TimeoutExpired:
            results.append({"id": entry["id"], "case_index": entry["case_index"],
                            "sanitizer": "timeout", "returncode": None})
            continue
        sanitizer = "clean" if proc.returncode == 0 else "other"
        for name, pattern in TEMPORAL_PATTERNS:
            if pattern.search(proc.stderr):
                sanitizer = name
                break
        results.append({
            "id": entry["id"], "case_index": entry["case_index"],
            "sanitizer": sanitizer, "returncode": proc.returncode,
        })
    return results, time.perf_counter() - start


def _reindex_source(source: str, index: int) -> str:
    """Rename a rendered case's colliding identifiers to a unique index."""
    source = re.sub(r"cand1_case_\d+", f"cand1_case_{index}", source)
    source = re.sub(r"CandItem\d+", f"CandItem{index}", source)
    source = re.sub(r"cand1_sink\d+", f"cand1_sink{index}", source)
    return source


def _run_mutation_asan(sources: dict[str, str], directory: Path) -> tuple[dict, float]:
    """ASan-confirm the EXTERN known-violation mutation cases.

    The reviewed external symbols get real definitions (EXTERN_HELPER_C)
    compiled into this binary only; the verifier analyzes case.c alone,
    where they are body-less annotated declarations. Every case must trip
    a temporal sanitizer error.
    """
    if not sources:
        return {}, 0.0
    ordered = sorted(sources)
    entries = [(902000 + offset, operator) for offset, operator in enumerate(ordered)]
    (directory / "mutation-violations.c").write_text(
        "\n".join(_reindex_source(sources[operator], index) for index, operator in entries),
        encoding="utf-8",
    )
    (directory / "externs.c").write_text(EXTERN_HELPER_C, encoding="utf-8")
    main = directory / "mutation-asan-main.c"
    declarations = "\n".join(f"int cand1_case_{index}(void);" for index, _ in entries)
    dispatch = "\n".join(f"        case {index}: return cand1_case_{index}();" for index, _ in entries)
    main.write_text(
        f"#include <stdlib.h>\n{declarations}\nint main(int argc, char **argv) {{\n"
        f"    if (argc != 2) return 2;\n    switch (atoi(argv[1])) {{\n{dispatch}\n"
        "        default: return 2;\n    }\n}\n", encoding="utf-8"
    )
    binary = directory / "mutation-asan"
    compile_proc = subprocess.run(
        ["clang", "-std=c11", "-O0", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(directory / "mutation-violations.c"), str(directory / "externs.c"), str(main),
         "-o", str(binary)],
        cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
    )
    if compile_proc.returncode != 0:
        return {operator: {"sanitizer": "compiler_rejected"} for operator in ordered}, 0.0
    results = {}
    start = time.perf_counter()
    for index, operator in entries:
        try:
            proc = subprocess.run([str(binary), str(index)], capture_output=True, text=True,
                                  timeout=30, check=False)
        except subprocess.TimeoutExpired:
            results[operator] = {"sanitizer": "timeout"}
            continue
        sanitizer = "clean" if proc.returncode == 0 else "other"
        for name, pattern in TEMPORAL_PATTERNS:
            if pattern.search(proc.stderr):
                sanitizer = name
                break
        results[operator] = {"sanitizer": sanitizer, "returncode": proc.returncode}
    return results, time.perf_counter() - start


def _mutation_cases(directory: Path, strict: StrictWorkspace) -> tuple[dict, dict, float]:
    bases = [
        render(make_case(0, 999998, "SAFE", "safe")),
        render(make_case(0, 999999, "SAFE", "safe-move")),
    ]
    report = {}
    asan_sources: dict[str, str] = {}
    start = time.perf_counter()
    for mutation, expected_class in OPERATORS.items():
        source = next((apply(base, mutation) for base in bases if apply(base, mutation) != base), bases[0])
        validate_mutation(source, mutation)
        case = make_case(0, 900000 + len(report), expected_class, "safe")
        result, returncode, _stdout, _stderr = strict.run(source)
        verdict = classify(case, result, returncode)
        report[mutation] = {
            "expected_class": expected_class,
            "compiler_valid": result.get("result") in {"pass", "fail", "incomplete"},
            "cand_result": result.get("semantic_result", result.get("result")),
            "qualification": verdict,
        }
        if mutation.startswith("EXTERN_") and expected_class == "KNOWN_VIOLATION":
            asan_sources[mutation] = source
    return report, asan_sources, time.perf_counter() - start


def run(seed: int, count: int, cand: Path, output: Path) -> dict:
    corpus = output / "corpus"
    manifest = write_corpus(seed, count, corpus)
    entries = manifest["cases"]
    buckets = Counter()
    mechanism_coverage: dict[str, Counter] = defaultdict(Counter)
    analysis_seconds = 0.0
    case_results = []
    with StrictWorkspace(cand) as strict, tempfile.TemporaryDirectory(prefix="cand1-c-runner-") as temp_name:
        directory = Path(temp_name)
        workers = min(8, os.cpu_count() or 1)
        start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=workers) as pool:
            analyzed = list(pool.map(lambda item: _analyze_entry(cand, corpus, item), entries))
        analysis_seconds += time.perf_counter() - start
        for entry, (report, returncode, _stdout, actual, _stderr) in zip(entries, analyzed):
            case = as_case(entry)
            verdict = classify(case, report, returncode)
            buckets[verdict] += 1
            for mechanism in actual:
                mechanism_coverage[mechanism][verdict] += 1
            case_results.append({"id": entry["id"], "class": case.semantic_class,
                                 "mechanisms": list(actual), "cand": report.get("result"),
                                 "qualification": verdict})
            if verdict == "FALSE_PASS":
                reduced = ROOT / "tests/regressions/false-pass" / f"{entry['id']}.c"
                minimize(cand, corpus / entry["source"], reduced)

        known = [entry for entry in entries if entry["semantic_class"] == "KNOWN_VIOLATION"]
        asan_results, asan_seconds = _run_asan_cases(known, corpus, directory)
        analysis_seconds += asan_seconds
        mutation_report, mutation_asan_sources, mutation_seconds = _mutation_cases(directory, strict)
        analysis_seconds += mutation_seconds
        mutation_asan, mutation_asan_seconds = _run_mutation_asan(mutation_asan_sources, directory)
        analysis_seconds += mutation_asan_seconds

        representative = corpus / entries[0]["source"]
        first = strict.run(representative.read_text(encoding="utf-8"))[2]
        second = strict.run(representative.read_text(encoding="utf-8"))[2]
        deterministic = first == second
    for workspace in _WORKER_WORKSPACES:
        workspace.close()
    _WORKER_WORKSPACES.clear()

    asan_counts = Counter(item["sanitizer"] for item in asan_results)
    asan_temporal = sum(count for name, count in asan_counts.items()
                        if name in {key for key, _ in TEMPORAL_PATTERNS})
    if asan_counts["compiler_rejected"] or asan_counts["timeout"]:
        raise RuntimeError("ASan corpus did not compile")
    result_by_id = {item["id"]: item for item in case_results}
    asan_false_pass = [item["id"] for item in asan_results
                       if item["sanitizer"] in {key for key, _ in TEMPORAL_PATTERNS}
                       and result_by_id.get(item["id"], {}).get("cand") == "pass"]
    mutation_correct = sum(item["qualification"] in {"CORRECT_PASS", "CORRECT_FAIL", "CORRECT_INCOMPLETE"}
                           for item in mutation_report.values())
    protocol = run_protocol_corpus()
    required_pass = max(1, count // 4)
    report = {
        "schema": "cand.fuzz-report/v2",
        "generator": GENERATOR_VERSION,
        "profile": "cand1/v1",
        "seed": seed,
        "cases": count,
        "provenance": _provenance(cand),
        "results": {key.lower(): buckets[key] for key in (
            "CORRECT_PASS", "CORRECT_FAIL", "CORRECT_INCOMPLETE", "COVERAGE_GAP",
            "FALSE_PASS", "FALSE_POSITIVE", "WRONG_FAILURE_CLASS", "HARNESS_ERROR",
        )},
        "mechanisms_declared": sorted({m for entry in entries for m in entry["mechanisms"]}),
        "mechanisms_exercised": sorted(mechanism_coverage),
        "mechanism_coverage": {
            key: {name.lower(): counts[name] for name in sorted(counts)}
            for key, counts in sorted(mechanism_coverage.items())
        },
        "asan": {
            "individually_executed_cases": len(asan_results),
            "counts": dict(sorted(asan_counts.items())),
            "temporal_confirmations": asan_temporal,
            "runtime_exercisable_violations": sum(
                count for name, count in asan_counts.items()
                if name not in {"clean", "other", "compiler_rejected", "timeout"}),
            "non_runtime_or_unconfirmed": asan_counts["clean"] + asan_counts["other"],
            "false_pass_cases": asan_false_pass,
            "cases": asan_results,
        },
        "mutations": {
            "operators": len(OPERATORS), "semantically_executed": len(mutation_report),
            "correct": mutation_correct, "operator_names": sorted(OPERATORS),
            "cases": mutation_report,
            "asan": mutation_asan,
        },
        "protocol": protocol,
        "deterministic_json": deterministic,
        "case_results": case_results,
        "analysis_seconds": round(analysis_seconds, 3),
        "cases_per_second": round(count / analysis_seconds, 2) if analysis_seconds else 0,
        "qualification": {"minimum_correct_pass": required_pass},
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if not deterministic or protocol["authority_bypasses"] or asan_false_pass:
        raise RuntimeError("determinism or protocol authority gate failed")
    if buckets["CORRECT_PASS"] < required_pass:
        raise RuntimeError(f"positive PASS coverage gap: {buckets['CORRECT_PASS']}/{required_pass}")
    if (buckets["FALSE_PASS"] or buckets["FALSE_POSITIVE"] or buckets["HARNESS_ERROR"]
            or buckets["COVERAGE_GAP"] or buckets["WRONG_FAILURE_CLASS"]):
        raise RuntimeError("differential qualification gate failed")
    if any(item["qualification"] not in {"CORRECT_PASS", "CORRECT_FAIL", "CORRECT_INCOMPLETE"}
           for item in mutation_report.values()):
        raise RuntimeError("mutation qualification gate failed")
    if any(item["sanitizer"] not in {key for key, _ in TEMPORAL_PATTERNS}
           for item in mutation_asan.values()):
        raise RuntimeError("EXTERN mutation ASan confirmation gate failed")
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
    print(json.dumps({"schema": report["schema"], "seed": args.seed, "cases": count,
                      "results": report["results"], "false_pass": report["results"]["false_pass"],
                      "correct_pass": report["results"]["correct_pass"],
                      "protocol_cases": report["protocol"]["cases"],
                      "cases_per_second": report["cases_per_second"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
