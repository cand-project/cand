#!/usr/bin/env python3
"""Generate a deterministic C&1-C corpus from (seed, version, profile)."""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

from taxonomy import GENERATOR_VERSION, MECHANISMS, make_case, render

PROFILES = {"fast": 1000, "extended": 10000}

TEMPLATES = {
    "SAFE": ("safe", "move"),
    "KNOWN_VIOLATION": ("violation", "double-free", "borrow-violation", "loop"),
    "UNSUPPORTED": ("unsupported", "unsupported-wrapper"),
}


def build_cases(seed: int, count: int) -> list:
    rng = random.Random(seed)
    cases = []
    for index in range(count):
        semantic_class = ("SAFE", "KNOWN_VIOLATION", "UNSUPPORTED")[index % 3]
        template = TEMPLATES[semantic_class][rng.randrange(len(TEMPLATES[semantic_class]))]
        # Rotating the catalog forces mechanism diversity independently of Python's
        # hash seed and makes every seed cover the full transport/CFG vocabulary.
        primary = MECHANISMS[(index + rng.randrange(len(MECHANISMS))) % len(MECHANISMS)]
        secondary = MECHANISMS[(index * 7 + seed) % len(MECHANISMS)]
        mechanisms = tuple(dict.fromkeys((primary, secondary, "allocation")))
        cases.append(make_case(seed, index, semantic_class, template, mechanisms))
    return cases


def write_corpus(seed: int, count: int, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    sources = output / "sources"
    sources.mkdir(exist_ok=True)
    cases = build_cases(seed, count)
    manifest = {
        "schema": "cand.fuzz-corpus/v1",
        "generator_version": GENERATOR_VERSION,
        "profile_version": "cand1/v1",
        "seed": seed,
        "cases": [],
    }
    for case in cases:
        path = sources / f"{case.id}.c"
        path.write_text(render(case), encoding="utf-8")
        entry = case.manifest()
        entry["source"] = str(path.relative_to(output))
        manifest["cases"].append(entry)
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--cases", type=int)
    parser.add_argument("--profile", choices=tuple(PROFILES))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.cases is None and args.profile is None:
        parser.error("one of --cases or --profile is required")
    count = args.cases if args.cases is not None else PROFILES[args.profile]
    if count < 1:
        parser.error("--cases must be positive")
    manifest = write_corpus(args.seed, count, args.output)
    print(json.dumps({
        "schema": manifest["schema"], "seed": args.seed,
        "generator_version": GENERATOR_VERSION, "cases": len(manifest["cases"]),
        "output": str(args.output),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
