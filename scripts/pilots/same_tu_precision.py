#!/usr/bin/env python3
"""Same-TU summary-precision taxonomy (milestone #61 Gate A evidence).

Classifies, for one pinned project measured in the E1 frame, every
same-TU function whose body-derived FunctionSummary contains an Unknown
position, by the *reason* the verifier's own SummaryBuilder recorded for
that Unknown.  Reasons come from a measurement build of the verifier
(``CAND_DUMP_SUMMARIES`` dump produced by ``cross_tu_pareto.py`` runs);
this script adds no semantics and draws no verdicts.

Method
------
1. Reuse a ``cross_tu_pareto.py`` workdir verbatim: the cached clang AST
   dumps (``ast/``), the raw cand report (``report.json``) and the
   per-TU summary dump with reasons (``summaries.jsonl``).  The project
   facts (function table, call sites, symbol classification) are rebuilt
   from the same cached ASTs, so both harnesses see identical inputs.
2. Attribute every obligation row to a blocker family exactly as
   ``cross_tu_pareto.py`` does (EXT / XTU / STU / IND / ALIAS / OTHER).
3. Join per-function status (CLEAR / blocked, blocking families) with
   the summary records and their recorded reasons, and classify each
   Unknown position into the H1-H15 same-TU precision taxonomy.
4. For every sole-STU blocked function, collect the categories that
   keep its same-TU callees undecided, yielding a static (upper-bound)
   CLEAR-gain estimate per candidate precision rule.  Measured
   counterfactuals (rule builds re-run on the pilots) remain the
   decision-grade numbers; this census only ranks candidates.

The reason codes recorded by the measurement build:

  return-side: unresolved-callee-return, borrow-origin-not-parameter,
    local-return (detail: param-alias | call:<callee> | member |
    address-of-member | address-of-local | local-alias | no-init | ...),
    unsupported-return-expr, conflicting-return-origins,
    pointer-return-unobserved, realloc-conflict, conflict-collapse
  param-side: conditional-effect (detail: "<would-be effect> from
    <callee> under <construct>"), callee-unknown-effect,
    unresolved-callee-effect, callee-arity-mismatch,
    multiple-parameter-identity, conflicting-effects,
    pointer-to-pointer, conflict-collapse

See ``docs/pilots/SAME-TU-SUMMARY-PRECISION-PARETO.md``.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cross_tu_pareto import ProjectFacts, ALIAS_KINDS, CALL_KINDS  # noqa: E402

# H-taxonomy: (code, description).  Categories measure *why a summary
# position is Unknown*; they are blocker shapes, not verdicts.
H_DOC = {
    'H1': 'conditional borrow / no-effect call collapsed to Unknown',
    'H2': 'conditional consume/destroy correctly held Unknown (fail-closed)',
    'H3': 'local pointer alias of parameter returned',
    'H4': 'local alias of owned return',
    'H5': 'returned local with no modeled provenance',
    'H6': 'struct/member/interior pointer returned via local',
    'H8': 'multiple return paths with conflicting origins',
    'H9': 'wrapper around decided callee via local (return side)',
    'H10': 'wrapper around undecided callee via local (return side)',
    'H11ext': 'call to external/system callee with unknown effect',
    'H11stu': 'call to same-TU callee whose own summary is undecided',
    'H11xtu': 'call to project callee in another TU (unknown effect)',
    'H11ind': 'indirect/callback call',
    'H12': 'pointer-to-pointer parameter',
    'H13': 'realloc / whole-summary conflict collapse',
    'H14': 'recursion / fixed-point cycle participant',
    'H15conflict': 'genuinely conflicting joined effects (fail-closed)',
    'H15other': 'other unsupported shape',
    'H16': 'reassigned borrow-origin parameter (post-#62 sound floor; not a precision candidate)',
}

COND_WOULDBE = re.compile(r'^(\S+(?: \S+)*?) from (.+?) under (if|switch|while|for|do|condop|nested)$')


def parse_conditional_detail(detail):
    """'borrow from helper under if' -> ('borrow', 'helper')."""
    m = COND_WOULDBE.match(detail)
    if not m:
        return None, None
    effect = m.group(1)
    callee = m.group(2)
    return effect, callee


def parse_conflict_detail(detail):
    """'borrow vs consumes at free' -> ('borrow', 'consumes', 'free')."""
    m = re.match(r'^(\S+) vs (\S+)(?: at (.+))?$', detail)
    if not m:
        return None, None, None
    return m.group(1), m.group(2), m.group(3)


def callee_family(sym, facts):
    """EXT / STU / XTU / IND for a callee name, using project facts."""
    if sym is None or sym == 'indirect' or sym.startswith('member:'):
        return 'IND'
    cat = facts.classify_symbol(sym)
    if cat == 'E':
        return 'IND'
    if cat in ('A', 'B', 'C'):
        return 'EXT'
    # project-local: same-TU needs a definition in a measured file
    for f in facts.proj_defs.get(sym, []):
        if f in facts.measured_files:
            return 'STU'
    return 'XTU'


def classify_reason(reason, facts):
    """One reason record -> H taxonomy code (or None if not blocking)."""
    code = reason['reason']
    detail = reason['detail']
    if code == 'conditional-effect':
        wouldbe, callee = parse_conditional_detail(detail)
        if wouldbe in ('no_ownership_effect', 'borrow'):
            return 'H1'
        if wouldbe in ('consumes', 'destroys'):
            return 'H2'
        fam = callee_family(callee, facts)
        return 'H11' + ('stu' if fam == 'STU' else
                        'xtu' if fam == 'XTU' else
                        'ind' if fam == 'IND' else 'ext')
    if code == 'local-return':
        if detail == 'param-alias':
            return 'H3'
        if detail.startswith('call:'):
            callee = detail[5:]
            # classify by the callee's own recorded summary, if measured
            rec = facts.summary_by_name.get(callee)
            if rec is not None:
                ret = rec['return']
                if ret == 'owned':
                    return 'H4'
                if ret == 'borrow_from_arg':
                    return 'H9'
                return 'H10'
            fam = callee_family(callee, facts)
            return 'H10' if fam in ('STU', 'XTU') else 'H4'
        if detail in ('member', 'address-of-member'):
            return 'H6'
        if detail == 'address-of-local':
            return 'H6'
        return 'H5'
    if code in ('unresolved-callee-effect', 'callee-unknown-effect',
                'unresolved-callee-return', 'borrow-origin-not-parameter',
                'callee-arity-mismatch'):
        sym = detail if code != 'conditional-effect' else None
        fam = callee_family(sym if sym != 'indirect' else None, facts) \
            if sym else 'IND'
        if code == 'borrow-origin-not-parameter':
            return 'H15other'
        if code == 'callee-arity-mismatch':
            return 'H15other'
        return 'H11' + fam.lower()
    if code == 'pointer-to-pointer':
        return 'H12'
    if code in ('realloc-conflict', 'conflict-collapse'):
        return 'H13'
    if code == 'conflicting-return-origins':
        return 'H8'
    if code == 'conflicting-effects':
        return 'H15conflict'  # refined later against cascade reasons
    if code in ('unsupported-return-expr', 'pointer-return-unobserved'):
        return 'H15other'
    if code == 'reassigned-parameter-origin':
        # incident #62 repair (ADR-0025): deliberately undecided; making
        # these decidable requires flow-sensitive origin tracking and is
        # out of scope for bounded same-TU precision
        return 'H16'
    return 'H15other'


def decided(record):
    if record['conflict']:
        return False
    if record['return'] == 'unknown':
        return False
    return all(p != 'unknown' for p in record['params'])


def load_summaries_with_reasons(path):
    """(name,file,line) -> list of records (one per defining TU)."""
    recs = collections.defaultdict(list)
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        recs[(r['name'], r['file'], r['line'])].append(r)
    return recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--file", action="append", required=True)
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--std", default="gnu11")
    ap.add_argument("--generated", action="append", default=[])
    ap.add_argument("--workdir", required=True,
                    help="cross_tu_pareto workdir to reuse (ast/, report.json, summaries.jsonl)")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    facts = ProjectFacts(args.project, args.root, args.file, args.include,
                         args.define, args.std, os.path.join(args.workdir, "ast"),
                         args.generated)
    facts.ALLOC_LIKE = set()
    facts.FREE_LIKE = set()
    report = json.load(open(os.path.join(args.workdir, "report.json")))
    summaries = load_summaries_with_reasons(os.path.join(args.workdir, "summaries.jsonl"))

    # name -> a representative record (for callee-summary lookup); records
    # that disagree across TUs are flagged, never silently merged.
    by_name = {}
    inconsistent = set()
    for (name, _f, _l), recs in summaries.items():
        uniq = {json.dumps({k: r.get(k) for k in ('return', 'borrow_arg', 'params', 'conflict')},
                           sort_keys=True) for r in recs}
        if len(uniq) > 1:
            inconsistent.add(name)
        by_name[name] = recs[0]
    facts.summary_by_name = by_name

    # ---------------------------------------------------------------- rows
    rows = []
    for o in report['unsupported']:
        loc = o['primary_location']
        sym = facts.resolve_symbol(o)
        rows.append({'kind': o['kind'], 'symbol': sym,
                     'fn': facts.function_for(loc['file'], loc['line']),
                     'file': loc['file'], 'line': loc['line'], 'col': loc.get('column')})

    def row_family(o, sym):
        base = o['kind'].split(':')[0]
        if base not in CALL_KINDS:
            return 'ALIAS' if base in ALIAS_KINDS else 'OTHER'
        suffixed = ':' in o['kind']
        cat = facts.classify_symbol(sym)
        if cat == 'E':
            return 'IND'
        if cat in ('A', 'B', 'C'):
            return 'EXT'
        if suffixed:
            return 'XTU'
        loc = o['primary_location']
        tu_defs = facts.same_tu_defs.get(os.path.realpath(loc['file']), set())
        if sym in tu_defs:
            return 'STU'
        return 'XTU'

    fn_blockers = collections.defaultdict(set)
    fn_rows = collections.defaultdict(list)
    for o, r in zip(report['unsupported'], rows):
        fam = row_family(o, r['symbol'])
        r['fam'] = fam
        if r['fn']:
            fn_blockers[r['fn']].add(fam)
            fn_rows[r['fn']].append(r)
    findings_fns = set()
    for f in report.get('findings', []):
        loc = f.get('primary_location', f.get('location', {}))
        fn = facts.function_for(loc['file'], loc.get('line'))
        if fn:
            findings_fns.add(fn)
    blocked = set(fn_blockers) | findings_fns
    clear = facts.allfns - blocked

    # ------------------------------------------------- per-function reasons
    # (name, file) -> record chosen by line containment in the function table
    fn_records = {}
    for (name, f, line), recs in summaries.items():
        for s, e, n in facts.byfile.get(os.path.realpath(f), []):
            if n == name and s <= line <= e:
                fn_records[(name, os.path.realpath(f))] = recs[0]
                break

    # fn name -> set of H categories blocking its summary
    fn_cats = collections.defaultdict(set)
    fn_reason_rows = collections.defaultdict(list)
    cat_occurrences = collections.Counter()
    cat_functions = collections.defaultdict(set)
    for (name, f), rec in fn_records.items():
        if decided(rec):
            continue
        # per-field reasons; conflicting-effects is a cascade when the same
        # field also has a conditional/unresolved reason in this function
        field_reasons = collections.defaultdict(list)
        for r in rec.get('reasons', []):
            field_reasons[r['field']].append(r)
        cats = set()
        for field, reasons in field_reasons.items():
            cascade = any(r['reason'] in ('conditional-effect', 'unresolved-callee-effect',
                                          'callee-unknown-effect') for r in reasons)
            for r in reasons:
                cat = classify_reason(r, facts)
                if cat == 'H15conflict' and cascade:
                    a, b, callee = parse_conflict_detail(r['detail'])
                    if a == 'unknown' or b == 'unknown':
                        # the unknown side came from a conditional or
                        # unresolved effect; attribute to the cascade source
                        src = [x for x in reasons
                               if x['reason'] in ('conditional-effect', 'unresolved-callee-effect',
                                                  'callee-unknown-effect')]
                        cat = classify_reason(src[0], facts) if src else 'H15conflict'
                cats.add(cat)
                cat_occurrences[cat] += 1
                fn_reason_rows[name].append({'field': field, 'cat': cat,
                                             'reason': r['reason'], 'detail': r['detail'],
                                             'line': r['line']})
        fn_cats[name] |= cats
        for c in cats:
            cat_functions[c].add(name)

    # ------------------------------------------------- sole-STU caller impact
    # For every sole-STU blocked function, which categories keep its
    # same-TU callees undecided?  (Static upper bound: a caller is
    # attributed to category C if some STU callee row's callee is an
    # undecided same-TU function blocked by C.)
    undecided_stu = {name for name, cats in fn_cats.items() if cats}
    fn_stu_callees = collections.defaultdict(set)
    for fn, rws in fn_rows.items():
        for r in rws:
            if r['fam'] != 'STU' or not r['symbol']:
                continue
            if r['symbol'] in undecided_stu:
                fn_stu_callees[fn].add(r['symbol'])
    sole_stu = {fn for fn, fams in fn_blockers.items() if fams == {'STU'}}
    # functions whose findings also block them are not CLEAR-able
    sole_stu = {fn for fn in sole_stu if fn not in findings_fns}

    cat_callers = collections.defaultdict(set)
    for fn in sole_stu:
        for callee in fn_stu_callees[fn]:
            for c in fn_cats.get(callee, set()):
                cat_callers[c].add(fn)

    # ------------------------------------------------------------- output
    out = {
        'project': args.project,
        'root': facts.root,
        'functions': len(facts.allfns),
        'clear': len(clear),
        'blocked': len(blocked & facts.allfns),
        'findings': len(report.get('findings', [])),
        'obligations': len(report['unsupported']),
        'summarized_functions': len(fn_records),
        'undecided_functions': len(fn_cats),
        'tu_inconsistent_summaries': sorted(inconsistent),
        'categories': {},
        'sole_stu_functions': len(sole_stu),
    }
    for cat in sorted(set(cat_occurrences) | set(cat_functions)):
        out['categories'][cat] = {
            'doc': H_DOC.get(cat, cat),
            'occurrences': cat_occurrences.get(cat, 0),
            'functions': len(cat_functions.get(cat, ())),
            'sole_stu_callers': len(cat_callers.get(cat, ())),
            'sample_functions': sorted(cat_functions.get(cat, ()))[:5],
            'sample_callers': sorted(cat_callers.get(cat, ()))[:5],
        }
    if args.json_out:
        with open(args.json_out, 'w') as fh:
            json.dump(out, fh, indent=1)
    # human summary
    print(json.dumps({k: v for k, v in out.items() if k != 'categories'}, indent=1))
    print(f"{'cat':12s} {'occ':>6s} {'fns':>6s} {'soleSTU':>8s}  description")
    for cat, c in sorted(out['categories'].items(), key=lambda kv: -kv[1]['occurrences']):
        print(f"{cat:12s} {c['occurrences']:6d} {c['functions']:6d} "
              f"{c['sole_stu_callers']:8d}  {c['doc']}")


if __name__ == '__main__':
    main()
