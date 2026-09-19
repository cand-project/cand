#!/usr/bin/env python3
"""Semantic one-operation mutations for known-good C fixtures."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

OPERATORS = {
    "SWAP_LIFETIME_EVENTS": "KNOWN_VIOLATION",
    "INSERT_ALIAS": "KNOWN_VIOLATION",
    "MOVE_FREE_EARLIER": "KNOWN_VIOLATION",
    "MOVE_FREE_LATER": "SAFE",
    "DUPLICATE_MOVE": "KNOWN_VIOLATION",
    "DUPLICATE_DESTROY": "KNOWN_VIOLATION",
    "ESCAPE_BORROW": "KNOWN_VIOLATION",
    "TRANSPORT_VIA_FIELD": "UNSUPPORTED",
    "TRANSPORT_VIA_ARRAY": "UNSUPPORTED",
    "TRANSPORT_VIA_MEMCPY": "UNSUPPORTED",
    "TRANSPORT_VIA_CAST": "UNSUPPORTED",
    "WRAP_IN_LOOP": "UNSUPPORTED",
    "WRAP_IN_BRANCH": "UNSUPPORTED",
    "CHANGE_DIRECT_CALL_TO_INDIRECT": "UNSUPPORTED",
}


def apply(source: str, mutation: str) -> str:
    if mutation not in OPERATORS:
        raise ValueError(f"unknown mutation: {mutation}")
    if mutation == "SWAP_LIFETIME_EVENTS" or mutation == "MOVE_FREE_EARLIER":
        return re.sub(
            r"(?m)^(\s*)(cand1_sink\d+ = p->value;)\n(\s*)free\(p\);$",
            r"\1free(p);\n\3\2", source, count=1,
        )
    if mutation == "MOVE_FREE_LATER":
        return re.sub(
            r"(?m)^(\s*)free\(p\);\n(\s*)(return [^;]+;)$",
            r"\2\3\n\1free(p);", source, count=1,
        )
    if mutation == "DUPLICATE_DESTROY":
        return source.replace("free(p);", "free(p);\n    free(p);", 1)
    if mutation == "DUPLICATE_MOVE":
        return source.replace("CAND_MOVE(p)", "CAND_MOVE(CAND_MOVE(p))", 1)
    if mutation == "INSERT_ALIAS":
        sink = re.search(r"cand1_sink\d+ = p->value;", source)
        replacement = "void *alias = p;\n    free(p);\n    " + (sink.group(0).replace("p->value", "((int *)alias)[0]") if sink else "return 0;")
        return source.replace("free(p);", replacement, 1)
    if mutation == "ESCAPE_BORROW":
        return source.replace("free(p);", "static void *escaped;\n    escaped = p;\n    free(p);", 1)
    if mutation == "TRANSPORT_VIA_FIELD":
        return source.replace("free(p);", "struct Holder { void *p; } holder = {p};\n    free(p);\n    p = holder.p;", 1)
    if mutation == "TRANSPORT_VIA_ARRAY":
        return source.replace("free(p);", "void *array[1] = {p};\n    free(p);\n    p = array[0];", 1)
    if mutation == "TRANSPORT_VIA_MEMCPY":
        return source.replace("free(p);", "void *copy = NULL;\n    memcpy(&copy, &p, sizeof copy);\n    free(p);\n    p = copy;", 1)
    if mutation == "TRANSPORT_VIA_CAST":
        return source.replace("free(p);", "free(p);\n    p = (void *)p;", 1)
    if mutation == "WRAP_IN_LOOP":
        return source.replace("free(p);", "for (int i = 0; i < 1; ++i) free(p);", 1)
    if mutation == "WRAP_IN_BRANCH":
        return source.replace("free(p);", "if (p != NULL) free(p);", 1)
    if mutation == "CHANGE_DIRECT_CALL_TO_INDIRECT":
        return source.replace("free(p);", "void (*destroy)(void *) = free;\n    destroy(p);", 1)
    raise AssertionError(mutation)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--mutation", choices=tuple(OPERATORS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = apply(args.base.read_text(encoding="utf-8"), args.mutation)
    args.output.write_text(result, encoding="utf-8")
    metadata = {
        "schema": "cand.fuzz-mutation/v1",
        "base_case": str(args.base),
        "mutation": args.mutation,
        "expected_class": OPERATORS[args.mutation],
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(metadata, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
