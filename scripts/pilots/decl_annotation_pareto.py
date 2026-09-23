#!/usr/bin/env python3
"""Declaration-annotation addressability census (milestone #39 Gate A).

Issue #39 asks whether declaration-site ownership annotations should seed
the same bounded summary path as reviewed contracts.  Before any
implementation, this harness measures the addressable population: for
every annotation-relevant obligation row (unknown pointer-return
ownership, unknown call with tracked pointer, unknown call with pointer
output), it classifies the callee by what a reviewed declaration-site
annotation could and could not resolve.

Buckets
-------
XTU-ELIGIBLE  project callee, defined in another measured TU, signature
              clean of pointer-to-pointer shapes, and a declaration is
              visible in the caller TU.  An annotation + review-manifest
              entry on the shared declaration resolves these rows in
              every caller TU.
XTU-GUARDED   project cross-TU callee whose signature contains a
              pointer-to-pointer parameter or return; the #41 scope
              guard must refuse seeding these (they stay INCOMPLETE).
XTU-NO-DECL   project cross-TU callee with no declaration visible in
              the caller TU (implicit declaration); annotating requires
              first adding a prototype.
STU           callee defined in the caller's own TU (same-TU undecided
              summary; milestone #61 scope, not annotation-addressable).
EXT-SYSTEM    ISO C / POSIX / compiler-runtime symbol (libc contract
              bundle domain, not declaration-annotation territory).
EXT-OTHER     external symbol not in the system tables.
IND           indirect / unresolved callee.

The harness performs no semantic changes and draws no verdicts; it only
measures.  It reuses a cross_tu_pareto workdir verbatim (cached ASTs,
report.json).  See docs/pilots/DECL-ANNOTATION-PROPAGATION.md.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cross_tu_pareto import ProjectFacts  # noqa: E402

# Obligation kinds whose resolution could come from a callee summary
# seeded by a declaration-site annotation.
ANNOTATION_KINDS = {
    'unknown-pointer-return-ownership',
    'unknown-call-with-tracked-pointer',
    'unknown-call-with-pointer-output',
    'unknown-call',
    'unknown-external-call',
}

BUCKET_DOC = {
    'XTU-ELIGIBLE': 'project cross-TU callee, clean signature, decl visible',
    'XTU-GUARDED': 'project cross-TU callee, pointer-to-pointer shape (#41 guard)',
    'XTU-NO-DECL': 'project cross-TU callee, no visible declaration in caller TU',
    'STU': 'callee defined in caller TU (#61 scope)',
    'EXT-SYSTEM': 'ISO C / POSIX / runtime symbol (contract-bundle domain)',
    'EXT-OTHER': 'external symbol outside system tables',
    'IND': 'indirect / unresolved callee',
}


def signature_has_pointer_to_pointer(facts: ProjectFacts, sym: str) -> bool | None:
    """True when any known parameter or return type of ``sym`` contains a
    pointer-to-pointer shape.  None when the signature is unknown (the
    definition was not measured); callers treat unknown as not eligible."""
    params = facts.fn_params.get(sym)
    if params is None:
        return None
    for p in params:
        if '**' in p:
            return True
    for types in facts.decl_types_tu.values():
        for qual in types.get(sym, ()):
            # qualType looks like 'char *sdsMakeRoomFor(sds, size_t)'.
            ret = qual.split('(')[0]
            if '**' in ret:
                return True
            break
    return False


def bucket_for(sym, facts: ProjectFacts, row_file: str) -> str:
    if sym is None or sym == 'indirect' or sym.startswith('member:'):
        return 'IND'
    cat = facts.classify_symbol(sym)
    if cat == 'E':
        return 'IND'
    if cat in ('A', 'B'):
        return 'EXT-SYSTEM'
    if cat == 'C':
        return 'EXT-OTHER'
    # project-local symbol ('D')
    tu_defs = facts.same_tu_defs.get(os.path.realpath(row_file), set())
    if sym in tu_defs:
        return 'STU'
    guarded = signature_has_pointer_to_pointer(facts, sym)
    if guarded is None:
        return 'XTU-NO-DECL'  # signature unknown: definition not measured
    if guarded:
        return 'XTU-GUARDED'
    decl_types = facts.decl_types_tu.get(os.path.realpath(row_file), {})
    if sym not in decl_types:
        return 'XTU-NO-DECL'
    return 'XTU-ELIGIBLE'


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

    bucket_rows = collections.Counter()
    bucket_kinds = collections.defaultdict(collections.Counter)
    bucket_symbols = collections.defaultdict(set)
    eligible_symbols = collections.Counter()
    measured = 0
    for o in report['unsupported']:
        base = o['kind'].split(':')[0]
        if base not in ANNOTATION_KINDS:
            continue
        measured += 1
        loc = o['primary_location']
        sym = facts.resolve_symbol(o)
        bucket = bucket_for(sym, facts, loc['file'])
        bucket_rows[bucket] += 1
        bucket_kinds[bucket][base] += 1
        if sym:
            bucket_symbols[bucket].add(sym)
            if bucket == 'XTU-ELIGIBLE':
                eligible_symbols[sym] += 1

    blocked = collections.Counter()
    for o in report['unsupported']:
        if o['kind'].split(':')[0] in ANNOTATION_KINDS:
            fn = facts.function_for(o['primary_location']['file'],
                                    o['primary_location']['line'])
            if fn:
                blocked[fn] += 1

    out = {
        'project': args.project,
        'annotation_relevant_rows': measured,
        'bucket_rows': dict(bucket_rows),
        'bucket_symbols': {k: sorted(v) for k, v in bucket_symbols.items()},
        'bucket_kind_rows': {k: dict(v) for k, v in bucket_kinds.items()},
        'xtu_eligible_distinct_symbols': len(eligible_symbols),
        'functions_with_annotation_relevant_rows': len(blocked),
    }
    if args.json_out:
        with open(args.json_out, 'w') as fh:
            json.dump(out, fh, indent=1, sort_keys=True)
    print(f"== {args.project}: {measured} annotation-relevant rows in "
          f"{len(blocked)} functions")
    for bucket in ('XTU-ELIGIBLE', 'XTU-GUARDED', 'XTU-NO-DECL', 'STU',
                   'EXT-SYSTEM', 'EXT-OTHER', 'IND'):
        print(f"   {bucket:13s} rows {bucket_rows[bucket]:6d}   "
              f"symbols {len(bucket_symbols[bucket]):5d}   {BUCKET_DOC[bucket]}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
