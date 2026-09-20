#!/usr/bin/env python3
"""Compare Hiredis pilot obligations without changing C& verdicts."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def walk(node, visit):
    if isinstance(node, dict):
        visit(node)
        for value in node.values():
            walk(value, visit)
    elif isinstance(node, list):
        for value in node:
            walk(value, visit)


def ast_functions(source_root: Path, source_file: str, ast_dir: Path | None):
    source = source_root / source_file
    ast_path = ast_dir / f"{source_file.replace('/', '_')}.json" if ast_dir else None
    if ast_path and ast_path.is_file():
        tree = json.loads(ast_path.read_text())
    else:
        with tempfile.NamedTemporaryFile() as output:
            command = ["clang-18", "-Xclang", "-ast-dump=json", "-fsyntax-only",
                       "-I.", "-std=c11", source_file]
            subprocess.run(command, cwd=source_root, stdout=output, check=True)
            output.seek(0)
            tree = json.load(output)

    lines = source.read_text().splitlines()
    functions = []

    def visit(node):
        if node.get("kind") != "FunctionDecl":
            return
        if not any(isinstance(child, dict) and child.get("kind") == "CompoundStmt"
                   for child in node.get("inner", [])):
            return
        loc = node.get("loc", {})
        begin = node.get("range", {}).get("begin", {})
        end = node.get("range", {}).get("end", {})
        if "includedFrom" in loc or "includedFrom" in begin:
            return
        start = loc.get("line")
        finish = end.get("line")
        if start is None:
            start = source.read_text()[:loc["offset"]].count("\n") + 1
        if finish is None:
            finish = source.read_text()[:end["offset"]].count("\n") + 1
        functions.append((node.get("name", "<anonymous>"), start, finish))

    walk(tree, visit)
    return functions


def function_for(functions, line):
    matches = [item for item in functions if item[1] <= line <= item[2]]
    return min(matches, key=lambda item: item[2] - item[1])[0] if matches else None


def location(item):
    return item.get("primary_location", {})


def observations(report, functions):
    rows = []
    for result in report["results"]:
        source_file = result["file"]
        for item in result["report"].get("analysis", {}).get("unsupported", []):
            loc = location(item)
            line = loc.get("line")
            rows.append({
                "file": source_file,
                "line": line,
                "column": loc.get("column"),
                "kind": item.get("kind", "other"),
                "mechanism": item.get("mechanism"),
                "symbol": item.get("symbol"),
                "function": function_for(functions.get(source_file, []), line)
                    if line is not None else None,
            })
    return rows


def key(row):
    return tuple(row.get(name) for name in
                 ("file", "line", "column", "kind", "mechanism", "symbol", "function"))


def finding_rows(report, functions):
    rows = []
    for result in report["results"]:
        source_file = result["file"]
        for item in result["report"].get("analysis", {}).get("findings", []):
            loc = item.get("primary_location", item.get("location", {}))
            line = loc.get("line")
            rows.append((source_file, line, function_for(functions.get(source_file, []), line)
                         if line is not None else None))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--ast-dir", type=Path)
    parser.add_argument("--mode", action="append", required=True,
                        metavar="NAME=REPORT.json")
    args = parser.parse_args()

    reports = {}
    for value in args.mode:
        name, path = value.split("=", 1)
        reports[name] = json.loads(Path(path).read_text())

    functions = {}
    for result in next(iter(reports.values()))["results"]:
        functions[result["file"]] = ast_functions(args.source_root, result["file"], args.ast_dir)

    rows = {name: observations(report, functions) for name, report in reports.items()}
    print("# Hiredis obligation comparison")
    for name, values in rows.items():
        print(f"{name}: total={len(values)} unique={len(set(map(key, values)))} "
              f"locations={len(set((r['file'], r['line'], r['column']) for r in values))}")
        for row in values:
            print("ROW", name, json.dumps(row, sort_keys=True))

    base = rows.get("H0", [])
    for name, values in rows.items():
        if name == "H0":
            continue
        removed = set(map(key, base)) - set(map(key, values))
        added = set(map(key, values)) - set(map(key, base))
        print(f"DELTA H0->{name}: removed={len(removed)} added={len(added)} "
              f"unchanged={len(set(map(key, base)) & set(map(key, values)))}")

    for name, report in reports.items():
        states = {source_file: {function: "CLEAR" for function, _, __ in ranges}
                  for source_file, ranges in functions.items()}
        for row in rows[name]:
            if row["function"]:
                states[row["file"]][row["function"]] = "BLOCKED"
        for source_file, line, function in finding_rows(report, functions):
            if function:
                states[source_file][function] = "VIOLATION"
        counts = {}
        for values in states.values():
            for state in values.values():
                counts[state] = counts.get(state, 0) + 1
        print(f"FUNCTIONS {name}: " + " ".join(f"{k}={v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
