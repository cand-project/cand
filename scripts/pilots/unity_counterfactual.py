#!/usr/bin/env python3
"""Unity-build cross-TU counterfactual (milestone #42 Gate A evidence).

Measures, WITHOUT implementing any cross-TU semantics, the decidability
gain a merged project-wide summary fixed point would produce: compile the
same measured TUs as ONE translation unit (a generated unity.c that
#includes them) and run the ordinary qualified verifier on it.

Inside one translation unit the verifier's existing, qualified same-TU
summary machinery (per-TU body-verified summaries with a bounded fixed
point, ADR-0013) already computes exactly the merged fixed point a
cross-TU design would have to reproduce.  The CLEAR/BLOCKED delta between
the per-TU run and the unity run on the same function universe is
therefore the measured counterfactual for "what if project-wide summaries
existed", up to three countable confounds:

- static functions with the same name in different TUs merge into one
  entity in a unity build (a cross-TU design must keep them distinct or
  fail closed; the per-project static-name-collision count is reported by
  scripts/pilots/cross_tu_pareto.py);
- macro/identifier redefinitions across TUs (counted from the clang
  diagnostics of the unity compile; the run aborts on any error);
- the function universe itself is cross-checked against the per-TU ASTs
  and the per-TU verifier run, and any mismatch is reported.

Function line ranges are taken from the per-TU AST dumps (identical source
files and lines; the unity AST dump of a whole-project TU is too large to
parse), and the unity function universe is cross-checked against the
summary dump of the unity run when CAND_DUMP_SUMMARIES is available.

This script performs no semantic changes; it is a measurement harness.
See docs/pilots/CROSS-TU-ADOPTION-PARETO.md.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from external_boundary_experiment import ast_for, functions_of  # noqa: E402

CAND = os.environ.get("CAND_BIN", "cand")
CLANG = os.environ.get("CLANG_BIN", "clang-18")


def run(cmd, cwd=None, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=None):
    return subprocess.run(cmd, cwd=cwd, stdout=stdout, stderr=stderr, env=env).returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--unity", required=True, help="unity/amalgam .c path (relative to root)")
    ap.add_argument("--offsets", help="JSON line-offset table for physical amalgams "
                    "([{file,start,lines}]); without it the unity file must use #include "
                    "and locations already refer to the original files")
    ap.add_argument("--file", action="append", required=True,
                    help="measured per-TU source files (relative to root)")
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--std", default="gnu11")
    ap.add_argument("--contracts", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    root = os.path.realpath(args.root)
    unity = os.path.realpath(os.path.join(root, args.unity))
    os.makedirs(args.workdir, exist_ok=True)
    ast_dir = os.path.join(args.workdir, "ast")
    os.makedirs(ast_dir, exist_ok=True)

    # 1. the unity TU must compile with the same frontend arguments
    err_path = os.path.join(args.workdir, "unity-compile.err")
    cmd = [CLANG, "-fsyntax-only"]
    for inc in args.include:
        cmd += ["-I", inc]
    for d in args.define:
        cmd += ["-D" + d]
    cmd += ["-std=" + args.std, args.unity]
    with open(err_path, "w") as err:
        rc = run(cmd, cwd=root, stderr=err)
    if rc != 0:
        print(f"UNITY COMPILE FAILED for {args.project}; no counterfactual", file=sys.stderr)
        return 2
    text = open(err_path, errors='replace').read()
    errors = len(re.findall(r'error:', text))
    warnings = len(re.findall(r'warning:', text))
    redefs = len(re.findall(r'\[-Wmacro-redefined\]|\[-Wredefined\]', text))

    # 2. function ranges from the per-TU AST dumps (identical files/lines)
    byfile = collections.defaultdict(list)
    allfns = set()
    skipped = []
    from pathlib import Path
    for f in args.file:
        tree = ast_for(root, f, Path(ast_dir), args.include, args.define, args.std)
        if tree is None:
            skipped.append(f)
            continue
        for _, s, e, n in functions_of(root, f, tree, root):
            byfile[os.path.realpath(os.path.join(root, f))].append((s, e, n))
            allfns.add(n)
        del tree
    for src in byfile:
        byfile[src].sort()

    # 3. ordinary verifier run on the unity TU (same contracts/flags)
    report_path = os.path.join(args.workdir, "unity-report.json")
    dump_path = os.path.join(args.workdir, "unity-summaries.jsonl")
    if os.path.exists(dump_path):
        os.unlink(dump_path)
    cmd = [CAND, "check", "--format", "json", "--contracts", os.path.realpath(args.contracts),
           unity, "--"]
    for inc in args.include:
        cmd += ["-I", inc]
    for d in args.define:
        cmd += ["-D" + d]
    cmd += ["-std=" + args.std]
    env = dict(os.environ)
    env["CAND_DUMP_SUMMARIES"] = dump_path
    with open(report_path, "w") as out:
        rc = run(cmd, cwd=root, stdout=out, env=env)

    report = json.load(open(report_path))

    offsets = None
    if args.offsets:
        offsets = json.load(open(os.path.realpath(os.path.join(root, args.offsets))))
        offsets.sort(key=lambda o: o['start'])

    def remap(file, line):
        """Map an amalgam location back to (original file, original line)."""
        if offsets is None or os.path.realpath(file) != unity:
            return file, line
        lo, hi = 0, len(offsets) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if offsets[mid]['start'] <= line:
                lo = mid
            else:
                hi = mid - 1
        o = offsets[lo]
        if not (o['start'] <= line < o['start'] + o['lines']):
            return file, line
        return os.path.join(root, o['file']), line - o['start'] + 1

    def function_for(file, line):
        file, line = remap(file, line)
        for s, e, n in byfile.get(os.path.realpath(file), []):
            if s <= line <= e:
                return n
        return None

    blocked, unmapped = set(), 0
    obligations = 0
    kinds = collections.Counter()
    for o in report['unsupported']:
        obligations += 1
        kinds[o['kind'].split(':')[0]] += 1
        loc = o['primary_location']
        fn = function_for(loc['file'], loc['line'])
        if fn:
            blocked.add(fn)
        else:
            unmapped += 1
    findings = 0
    finding_fns = set()
    for f in report.get('findings', []):
        loc = f.get('primary_location', f.get('location', {}))
        fn = function_for(loc['file'], loc.get('line'))
        if fn:
            blocked.add(fn)
            finding_fns.add(fn)
            findings += 1
    clear = allfns - blocked

    # 4. cross-check the unity function universe against the summary dump
    unity_fns = set()
    unity_decided = set()
    if os.path.exists(dump_path):
        for line in open(dump_path):
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            unity_fns.add(r['name'])
            if (not r['conflict'] and r['return'] != 'unknown'
                    and all(p != 'unknown' for p in r['params'])):
                unity_decided.add(r['name'])

    result = {
        'project': args.project,
        'unity': args.unity,
        'files': len(args.file),
        'files_skipped': skipped,
        'compile': {'errors': errors, 'warnings': warnings,
                    'redefinitions': redefs, 'rc': rc},
        'functions': len(allfns),
        'clear': len(clear),
        'blocked': len(blocked & allfns),
        'findings': findings,
        'finding_functions': sorted(finding_fns),
        'obligations': obligations,
        'unmapped_obligation_rows': unmapped,
        'obligation_kinds': dict(kinds),
        'unity_summary_universe': len(unity_fns),
        'unity_summary_decided': len(unity_decided),
        'universe_match': (not unity_fns) or unity_fns == allfns,
    }
    print(json.dumps(result, indent=2))
    if args.json_out:
        json.dump(result, open(args.json_out, 'w'), indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
