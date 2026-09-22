#!/usr/bin/env python3
"""External-boundary adoption experiment runner.

Runs the C& verifier over a pinned project scope under two contract
configurations and reports function-level CLEAR/BLOCKED transitions,
obligation deltas, and false-PASS guards.

Method (corrected during milestone #58; see
docs/pilots/EXTERNAL-CONTRACT-ADOPTION-RESULTS.md):

- whole-corpus single cand runs per configuration, default semantic
  profile, identical flags;
- the function universe comes from Clang AST main-file definition ranges
  (it must match cand's own coverage.functions_analyzed);
- a function is CLEAR when no obligation and no finding maps inside its
  body (range containment);
- gained/lost CLEAR sets and added/removed obligation sets are computed by
  exact key-set diffs of the two reports, never by per-run ad-hoc
  attribution.

The script exits nonzero on any regression (lost CLEAR or added
obligation).

Usage:
  external_boundary_experiment.py --project <name> --root <dir> \
      (--file <src.c>)... [--include <dir>]... [--define <D>]... \
      --e0 <contracts.yaml> --e1 <contracts.yaml> [--json-out <file>]

The script writes AST dumps to a scratch directory under
$TMPDIR/cand-ext-exp/<project> (reused across runs) and prints a
machine-readable summary on stdout.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import subprocess
import sys
import tempfile

CAND = os.environ.get("CAND_BIN", "cand")
CLANG = os.environ.get("CLANG_BIN", "clang-18")


def run_cand(files, includes, defines, contracts, out_path, err_path, std="gnu11"):
    cmd = [CAND, "check", "--format", "json", "--contracts", contracts]
    cmd += files
    cmd += ["--"]
    for inc in includes:
        cmd += ["-I", inc]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-std=" + std]
    with open(out_path, "w") as out, open(err_path, "w") as err:
        rc = subprocess.run(cmd, stdout=out, stderr=err).returncode
    return rc


def ast_for(source_root, source_file, ast_dir, includes, defines, std="gnu11"):
    """Return the clang AST JSON for one file (cached on disk), or None if
    the file does not compile under the pinned frontend flags.

    The cache key includes the frontend flags so that a run with different
    includes/defines can never reuse a stale or partial dump; a dump is
    only cached when clang exited 0 (a failed run never leaves a file)."""
    flag_key = hashlib.sha256(
        ("\x00".join(includes + defines) + "\x00" + std).encode()
    ).hexdigest()[:12]
    key = source_file.replace("/", "_") + "." + flag_key + ".json"
    path = ast_dir / key
    if path.is_file():
        return json.loads(path.read_text())
    cmd = [CLANG, "-Xclang", "-ast-dump=json", "-fsyntax-only"]
    for inc in includes:
        cmd += ["-I", inc]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-std=" + std, source_file]
    with open(path, "w") as out:
        ok = subprocess.run(cmd, cwd=source_root, stdout=out,
                            stderr=subprocess.DEVNULL).returncode == 0
    if not ok:
        path.unlink()
        return None
    return json.loads(path.read_text())


def functions_of(source_root, source_file, tree, root):
    """Main-file function definitions with line ranges."""
    source_path = os.path.realpath(os.path.join(source_root, source_file))
    source_names = {source_path, os.path.basename(source_file),
                    os.path.relpath(source_path, root)}
    out = []

    def walk(node, cur_line):
        if not isinstance(node, dict):
            return
        r = node.get("range", {}).get("begin", {})
        line = r.get("line", cur_line)
        if node.get("kind") == "FunctionDecl" and not node.get("isImplicit"):
            loc = node.get("loc", {})
            src = loc.get("file")
            in_main = ((src is None and "includedFrom" not in loc) or
                       (src is not None and (src in source_names or
                        os.path.realpath(os.path.join(root, src)) == source_path)))
            if in_main:
                if any(c.get("kind") == "CompoundStmt" for c in node.get("inner", [])):
                    end = node.get("range", {}).get("end", {}).get("line") or loc.get("line", 0)
                    out.append((source_path, loc.get("line", 0), end, node["name"]))
        for child in node.get("inner", []):
            walk(child, line)

    walk(tree, None)
    return out


def states_for(report_path, byfile, allfns):
    d = json.load(open(report_path))
    blocked = set()
    unmapped = 0
    for o in d["unsupported"]:
        loc = o["primary_location"]
        hit = None
        for s, e, n in byfile.get(os.path.realpath(loc["file"]), []):
            if s <= loc["line"] <= e:
                hit = n
                break
        if hit:
            blocked.add(hit)
        else:
            unmapped += 1
    for f in d.get("findings", []):
        loc = f.get("primary_location", f.get("location", {}))
        for s, e, n in byfile.get(os.path.realpath(loc["file"]), []):
            if s <= loc["line"] <= e:
                blocked.add(n)
                break
    return d, allfns - blocked, blocked, unmapped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--file", action="append", required=True)
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--e0", required=True)
    ap.add_argument("--e1", required=True)
    ap.add_argument("--json-out")
    ap.add_argument("--std", default="gnu11")
    args = ap.parse_args()

    root = os.path.realpath(args.root)
    scratch = os.path.join(tempfile.gettempdir(), "cand-ext-exp", args.project)
    os.makedirs(scratch, exist_ok=True)
    ast_dir = os.path.join(scratch, "ast")
    os.makedirs(ast_dir, exist_ok=True)
    from pathlib import Path

    byfile = collections.defaultdict(list)
    allfns = set()
    skipped = []

    for f in args.file:
        tree = ast_for(root, f, Path(ast_dir), args.include, args.define, args.std)
        if tree is None:
            skipped.append(f)
            continue
        for src, s, e, n in functions_of(root, f, tree, root):
            byfile[src].append((s, e, n))
            allfns.add(n)
    for f in byfile:
        byfile[f].sort()
    files = [os.path.join(root, f) for f in args.file if f not in skipped]

    e0_report = os.path.join(scratch, "e0.json")
    e1_report = os.path.join(scratch, "e1.json")
    rc0 = run_cand(files, args.include, args.define, os.path.realpath(args.e0),
                   e0_report, e0_report + ".err", args.std)
    rc1 = run_cand(files, args.include, args.define, os.path.realpath(args.e1),
                   e1_report, e1_report + ".err", args.std)

    d0, c0, b0, u0 = states_for(e0_report, byfile, allfns)
    d1, c1, b1, u1 = states_for(e1_report, byfile, allfns)

    key = lambda o: (o["kind"], o["primary_location"]["file"],
                     o["primary_location"]["line"], o["primary_location"]["column"])
    k0 = {key(o) for o in d0["unsupported"]}
    k1 = {key(o) for o in d1["unsupported"]}

    result = {
        "project": args.project,
        "root": root,
        "files": len(args.file),
        "files_analyzed": len(files),
        "files_skipped": skipped,
        "functions": len(allfns),
        "e0": {"result": d0["result"], "obligations": len(d0["unsupported"]),
               "findings": len(d0.get("findings", [])), "clear": len(c0),
               "blocked": len(b0), "unmapped": u0},
        "e1": {"result": d1["result"], "obligations": len(d1["unsupported"]),
               "findings": len(d1.get("findings", [])), "clear": len(c1),
               "blocked": len(b1), "unmapped": u1},
        "gained_clear": sorted(c1 - c0),
        "lost_clear": sorted(c0 - c1),
        "obligations_removed": len(k0 - k1),
        "obligations_added": len(k1 - k0),
        "findings_unchanged": len(d0.get("findings", [])) == len(d1.get("findings", [])),
    }
    print(json.dumps(result, indent=2))
    if args.json_out:
        json.dump(result, open(args.json_out, "w"), indent=2)
    # fail loudly on any regression
    if result["lost_clear"] or result["obligations_added"]:
        print("REGRESSION detected", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
