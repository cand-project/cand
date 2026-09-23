#!/usr/bin/env python3
"""Alias/storage evidence-bar Pareto (milestone #54).

Issue #54 sets an explicit evidence bar before any alias/storage
implementation: alias/storage precision becomes workable only when
(a) the cross-TU and external-API sole-blocker families have shrunk
enough that alias/storage is dominant, or (b) the CVE replay corpus
shows alias/storage as a recurring real-defect path.

This harness measures (a) on a cross_tu_pareto workdir (reused verbatim:
cached ASTs, report.json). It attributes every obligation row to a
finer-grained blocker family than the same-TU census (which folds all
non-call, non-alias kinds into OTHER) and reports, per function, the set
of blocking families. A function is *sole-blocked* by family X when every
obligation row in it belongs to X.

Families:
  ALIAS   ambiguous-alias-target, unresolved-pointee-storage  (#54 scope)
  PTR-OUT unknown-call-with-pointer-output                    (#41 scope)
  URET    unknown-pointer-return-ownership
  STU/XTU/EXT/IND  call-effect families (callee-resolved)
  FLOW    all other storage/flow kinds (arithmetic reassignment,
          global/static storage, owner overwrite, stack returns, ...)
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cross_tu_pareto import ProjectFacts, ALIAS_KINDS, CALL_KINDS  # noqa: E402

FAMILY_DOC = {
    'ALIAS': 'ambiguous-alias-target / unresolved-pointee-storage (#54)',
    'PTR-OUT': 'unknown-call-with-pointer-output (#41)',
    'URET': 'unknown-pointer-return-ownership',
    'STU': 'call to undecided same-TU callee',
    'XTU': 'call to project callee in another TU',
    'EXT': 'call to external/system callee with unknown effect',
    'IND': 'indirect/callback call',
    'FLOW': 'other storage/flow obligation kinds',
}


def row_family(kind: str, symbol, facts: ProjectFacts, loc: dict) -> str:
    base = kind.split(':')[0]
    if base in ALIAS_KINDS:
        return 'ALIAS'
    if base == 'unknown-call-with-pointer-output':
        return 'PTR-OUT'
    if base == 'unknown-pointer-return-ownership':
        return 'URET'
    if base in CALL_KINDS:
        cat = facts.classify_symbol(symbol)
        if cat == 'E':
            return 'IND'
        if cat in ('A', 'B', 'C'):
            return 'EXT'
        if ':' in kind:
            return 'XTU'
        tu_defs = facts.same_tu_defs.get(os.path.realpath(loc['file']), set())
        return 'STU' if symbol in tu_defs else 'XTU'
    return 'FLOW'


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--file', action='append', required=True)
    ap.add_argument('--include', action='append', default=[])
    ap.add_argument('--define', action='append', default=[])
    ap.add_argument('--std', default='gnu11')
    ap.add_argument('--generated', action='append', default=[])
    ap.add_argument('--workdir', required=True)
    ap.add_argument('--json-out')
    args = ap.parse_args()

    facts = ProjectFacts(args.project, args.root, args.file, args.include,
                         args.define, args.std, os.path.join(args.workdir, 'ast'),
                         args.generated)
    report = json.load(open(os.path.join(args.workdir, 'report.json')))

    fn_families = collections.defaultdict(set)
    fn_kinds = collections.defaultdict(collections.Counter)
    family_rows = collections.Counter()
    for o in report['unsupported']:
        loc = o['primary_location']
        sym = facts.resolve_symbol(o)
        fam = row_family(o['kind'], sym, facts, loc)
        fn = facts.function_for(loc['file'], loc['line'])
        family_rows[fam] += 1
        if fn:
            fn_families[fn].add(fam)
            fn_kinds[fn][o['kind'].split(':')[0]] += 1

    sole = collections.Counter()
    alias_affected = 0
    for fn, fams in fn_families.items():
        if len(fams) == 1:
            sole[fams.pop()] += 1
        if 'ALIAS' in fams:
            alias_affected += 1

    blocked = len(fn_families)
    out = {
        'project': args.project,
        'blocked_functions': blocked,
        'obligations': sum(family_rows.values()),
        'family_rows': dict(family_rows),
        'sole_blocked': dict(sole),
        'alias_affected_functions': alias_affected,
        'sole_blocked_functions': {
            fam: sorted(fn for fn, f in fn_families.items() if f == {fam})
            for fam in sorted(sole)
        },
    }
    if args.json_out:
        with open(args.json_out, 'w') as fh:
            json.dump(out, fh, indent=1, sort_keys=True)
    print(f"== {args.project}: {blocked} blocked functions, "
          f"{sum(family_rows.values())} obligations")
    for fam, n in sorted(sole.items(), key=lambda kv: -kv[1]):
        print(f"   sole-{fam:8s} {n:5d}   (rows {family_rows[fam]:6d})  {FAMILY_DOC[fam]}")
    print(f"   alias-affected functions (any ALIAS row): {alias_affected}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
