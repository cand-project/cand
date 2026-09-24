#!/usr/bin/env python3
"""Pointer-output addressability census (milestone #41 Gate A).

Issue #41 asks whether a narrowly specified ``produces_out_owner``
contract effect can convert ``unknown-call-with-pointer-output``
obligations into classified results.  Before any implementation, this
harness measures the addressable population: for every such obligation
row, it classifies the destination-argument shape (which storage the
callee would write and whether it is a genuine out-parameter), the
callee family, and the write-guard idiom used at the call site.

Scope note: the verifier emits ``unknown-call-with-pointer-output``
whenever ``mayWritePointerStorage`` accepts ANY call argument — an
address-of whose pointee type may contain a pointer, or a plain
pointer whose pointee may contain a pointer.  The kind therefore
covers three distinct populations:

* true out-parameters: a ``T **``-typed destination (``&local`` where
  the local is a single-level pointer, ``T **`` expressions, ...);
* aggregate writes: ``&local`` where the local is a struct containing
  pointers (the callee writes caller-owned fields);
* write-through pointers: a plain ``T *`` argument whose pointee
  contains pointers (the callee may write pointer fields through a
  borrowed pointer).

Only the first population is #41 territory; the census separates them.

Row buckets
-----------
ADDR-LOCAL          ``&v``, ``v`` a function-local single-level pointer
                    (a ``T **`` destination in single dereferenceable
                    local storage).  The only shape the bounded rule
                    could accept.
ADDR-PARAM          ``&p``, ``p`` a single-level pointer parameter.
ADDR-STATIC-LOCAL   ``&v``, ``v`` a static local pointer.
ADDR-GLOBAL         ``&g``, ``g`` a file-scope pointer.
ADDR-FIELD          ``&expr.field`` / ``&expr->field``, the field a
                    single-level pointer (aggregate storage).
ADDR-ELEMENT        ``&arr[i]`` or ``arr[i]`` with pointer element type.
BARE-PP             any other ``T **``-typed expression.
NESTED-PP           a ``T ***`` (or deeper) destination.
AGG-WRITE           only ``&struct``-shaped destinations (no genuine
                    out-parameter at the call).
PTR-ARG             only plain pointer arguments (write-through).
VA-BUILTIN          va_start / va_end / va_copy (not calls in the AST
                    dump; not out-parameter candidates).
UNMATCHED           row could not be located in the AST dump.

Guard buckets (write-condition idiom, source-statement heuristic)
-----------------------------------------------------------------
COND-GUARD         the call appears inside an ``if``/``while``/``switch``
                   condition (the classic return-guarded out-parameter).
ASSIGN-CHECKED     the result is assigned, then compared within three
                   statements.
ASSIGN-UNCHECKED   the result is assigned but never compared nearby.
UNCHECKED          the call result is discarded.

The guard classification is a documented source heuristic over the
statement containing the call; the argument-shape classification is
AST-derived (clang JSON dump, lines recomputed from offsets because
the dump omits ``line`` inside a file context, and referenced
declarations resolved through an id map because the dump abbreviates
them without locations).  The harness performs no semantic changes and
draws no verdicts; it only measures.  It reuses a cross_tu_pareto
workdir verbatim (cached ASTs, report.json).  See
docs/pilots/POINTER-OUTPUT-PARETO.md.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cross_tu_pareto import ProjectFacts, ast_for, callee_name, walk  # noqa: E402

PO_KIND = 'unknown-call-with-pointer-output'
VA_SYMBOLS = ('va_start', 'va_end', 'va_copy', '__builtin_va_start',
              '__builtin_va_end', '__builtin_va_copy')

CAST_KINDS = {'ImplicitCastExpr', 'ParenExpr', 'CStyleCastExpr',
              'FunctionalCastExpr', 'NoThrowExpr', 'FullExpr'}

# Priority-ordered row buckets for genuine out-parameter shapes.
PO_SHAPES = ['ADDR-LOCAL', 'ADDR-PARAM', 'ADDR-STATIC-LOCAL', 'ADDR-GLOBAL',
             'ADDR-FIELD', 'ADDR-ELEMENT', 'BARE-PP']

ROW_DOC = {
    'ADDR-LOCAL': 'address of a function-local single-level pointer, direct callee (T ** destination in local storage)',
    'ADDR-LOCAL-INDIRECT': 'address of a function-local single-level pointer, INDIRECT callee (no contract bind point)',
    'ADDR-PARAM': 'address of a single-level pointer parameter',
    'ADDR-STATIC-LOCAL': 'address of a static local pointer',
    'ADDR-GLOBAL': 'address of a file-scope pointer',
    'ADDR-FIELD': 'address of a struct field (single-level pointer field)',
    'ADDR-ELEMENT': 'address of an array element with pointer type',
    'BARE-PP': 'other T **-typed expression',
    'NESTED-PP': 'T *** or deeper destination',
    'AGG-WRITE': 'only &struct-shaped destinations (aggregate writes)',
    'PTR-ARG': 'only plain pointer arguments (write-through)',
    'VA-BUILTIN': 'va_start / va_end / va_copy row',
    'UNMATCHED': 'row not located in the AST dump',
}

GUARD_DOC = {
    'COND-GUARD': 'call inside an if/while/switch condition',
    'ASSIGN-CHECKED': 'result assigned then compared nearby',
    'ASSIGN-UNCHECKED': 'result assigned, not compared nearby',
    'UNCHECKED': 'result discarded',
}


def strip_casts(node):
    for _ in range(6):
        if node.get('kind') in CAST_KINDS:
            inner = node.get('inner') or []
            if not inner:
                return node
            node = inner[0]
            continue
        return node
    return node


def dest_flavor(qual):
    """Classify the type of the storage a ``&expr`` designates.

    SCALAR-PTR: single-level pointer (a genuine ``T **`` destination).
    PP: pointer-to-pointer (a ``T ***`` destination — nested, refused).
    AGG: struct/union/array/scalar (aggregate or non-pointer write).
    OTHER: function pointers and other shapes this census does not
    model.
    """
    q = (qual or '').strip()
    if not q or re.search(r'\(\s*\*', q):
        return 'OTHER'
    if re.search(r'\*\s*\*', q):
        return 'PP'
    if q.endswith('*'):
        return 'SCALAR-PTR'
    return 'AGG'


class CallSiteCollector:
    """AST walk that records CallExpr argument shapes with function context.

    clang's JSON AST dump omits the ``line`` field whenever a location
    stays inside the previously printed file context, so every line in
    this collector is recomputed from the (always present) ``offset``
    against a line-start table of the dumped source file.  The dump
    also abbreviates ``referencedDecl`` nodes without a ``loc``; full
    declaration nodes are indexed by id and resolved through that map.
    """

    def __init__(self, root, rel, tree):
        self.root = root
        self.rel = rel
        self.src = os.path.realpath(os.path.join(root, rel))
        with open(self.src, encoding='utf-8', errors='replace') as fh:
            self.text = fh.read()
        starts = [0]
        for i, ch in enumerate(self.text):
            if ch == '\n':
                starts.append(i + 1)
        self.line_starts = starts
        self.sites = {}      # (line, col) -> record
        self.fn_ranges = []  # (start, end, name) sorted
        self.decl_map = {}   # node id -> full decl node (with loc)
        self._collect_decls(tree)
        self._collect_functions(tree)
        self._walk(tree)

    def _collect_decls(self, tree):
        """Index declaration facts by node id.

        clang's JSON dump abbreviates ``referencedDecl`` nodes without a
        ``loc``; the full declaration node (with its location) is only
        present where it is declared, so arguments are resolved through
        this id map.  Only extracted scalars are stored — retaining
        node references would pin the entire parsed tree (and every
        tree before it) in memory.
        """
        stack = [tree]
        while stack:
            node = stack.pop()
            if not isinstance(node, dict):
                continue
            if node.get('kind') in ('VarDecl', 'ParmVarDecl', 'FunctionDecl') \
                    and node.get('id'):
                entry = {
                    'kind': node.get('kind'),
                    'storageClass': node.get('storageClass'),
                    'loc': node.get('loc', {}),
                    'name': node.get('name'),
                }
                if node.get('kind') == 'FunctionDecl':
                    qual = (node.get('type') or {}).get('qualType', '')
                    entry['variadic'] = '...' in qual
                if node.get('kind') == 'VarDecl':
                    init = next((c for c in node.get('inner') or []
                                 if c.get('kind') != 'ParmVarDecl'), None)
                    entry['has_init'] = init is not None
                    if init is not None:
                        # Null-initialization shapes: NULL (macro-expands
                        # to cast/paren-wrapped 0), 0, nullptr — strip
                        # wrappers before testing.
                        probe = strip_casts(init)
                        k = probe.get('kind', '')
                        entry['null_init'] = (
                            k == 'CStyleNullPtrExpr' or
                            (k == 'IntegerLiteral' and
                             (probe.get('value') in ('0', '00'))) or
                            k == 'GNUNullExpr')
                self.decl_map.setdefault(node['id'], entry)
            for child in node.get('inner') or []:
                stack.append(child)

    def line_of(self, offset):
        import bisect
        if offset is None:
            return None
        return bisect.bisect_right(self.line_starts, offset - 1)

    def node_line(self, node, which='begin'):
        r = node.get('range', {}).get(which, {})
        if 'expansionLoc' in r:
            # Macro-expanded expression: use the invocation site, not
            # the macro definition (matches the obligation location).
            r = r['expansionLoc']
        if r.get('file') is not None or 'includedFrom' in r:
            return r.get('line')  # location in another file: trust line only
        if r.get('line') is not None:
            return r['line']
        if r.get('offset') is not None:
            return self.line_of(r['offset'])
        return r.get('line')

    def _collect_functions(self, tree):
        """Collect main-file function definition ranges.

        Only FunctionDecl nodes that have a body and whose range begins
        in the main file (no ``file``/``includedFrom`` on the location)
        are counted; header declarations carry offsets from other files
        and would poison the range table.
        """
        def visit(node):
            if node.get('kind') == 'FunctionDecl' and node.get('name'):
                r = node.get('range', {}).get('begin', {})
                if r.get('file') is not None or 'includedFrom' in r:
                    return
                has_body = any(c.get('kind') == 'CompoundStmt'
                               for c in node.get('inner') or [])
                if not has_body:
                    return
                start = self.node_line(node, 'begin')
                end = self.node_line(node, 'end')
                if start is not None:
                    self.fn_ranges.append((start, end or start, node['name']))
        walk(tree, visit)
        self.fn_ranges.sort()

    def _function_at(self, line):
        for start, end, name in self.fn_ranges:
            if start <= line <= end:
                return name
        return None

    def _walk(self, tree):
        stack = [tree]
        while stack:
            node = stack.pop()
            if not isinstance(node, dict):
                continue
            if node.get('kind') == 'CallExpr':
                r = node.get('range', {}).get('begin', {})
                if 'expansionLoc' in r:
                    r = r['expansionLoc']
                line = self.node_line(node)
                col = r.get('col') or r.get('column')
                if col is not None and line is not None:
                    args = [strip_casts(a) for a in (node.get('inner') or [])[1:]]
                    classified = [classify_arg(a, self) for a in args]
                    self.sites[(line, col)] = {
                        'callee': callee_name(node),
                        'direct': self._callee_is_direct(node),
                        'variadic': self._callee_is_variadic(node),
                        'callee_resolved': self._callee_resolved(node),
                        'args': [(b, f) for b, f, _ in classified],
                        'out_decls': [d for b, _, d in classified
                                      if b == 'ADDR-LOCAL' and d],
                        'fn': self._function_at(line),
                    }
            for child in reversed(node.get('inner') or []):
                stack.append(child)

    def _callee_is_variadic(self, node):
        inner = node.get('inner') or []
        if not inner:
            return False
        callee = strip_casts(inner[0])
        ref = callee.get('referencedDecl') or {}
        resolved = self.decl_map.get(ref.get('id')) or {}
        return bool(resolved.get('variadic'))

    def _callee_resolved(self, node):
        inner = node.get('inner') or []
        if not inner:
            return False
        callee = strip_casts(inner[0])
        ref = callee.get('referencedDecl') or {}
        return ref.get('id') in self.decl_map

    def _callee_is_direct(self, node):
        """True when the callee resolves to a named FunctionDecl.

        Indirect calls (function pointers, member callbacks) cannot be
        bound to a symbol-named contract, so their rows are not
        addressable by the bounded rule however well-shaped the
        destination is.
        """
        inner = node.get('inner') or []
        if not inner:
            return False
        callee = strip_casts(inner[0])
        ref = callee.get('referencedDecl') or {}
        if ref.get('kind') == 'FunctionDecl':
            return True
        resolved = self.decl_map.get(ref.get('id')) or {}
        return resolved.get('kind') == 'FunctionDecl'

    def resolve_decl(self, inner):
        """Return the full declaration node for a DeclRefExpr."""
        ref = inner.get('referencedDecl') or {}
        node = self.decl_map.get(ref.get('id'))
        return node if node is not None else ref

    def decl_line(self, decl):
        """Line of a declaration inside the dumped main file, else None."""
        loc = decl.get('loc', {})
        if 'expansionLoc' in loc:
            loc = loc['expansionLoc']
        if loc.get('file') is not None or 'includedFrom' in loc:
            return None  # header/other-file declaration: not a local
        if loc.get('line') is not None:
            return loc['line']
        if loc.get('offset') is not None:
            return self.line_of(loc['offset'])
        return None

    def inside_function(self, decl):
        line = self.decl_line(decl)
        if line is None:
            return False
        return any(s <= line <= e for s, e, _ in self.fn_ranges)


def classify_arg(arg, collector):
    """Classify one call argument as a destination shape.

    Returns (bucket, flavor, decl) where decl is the resolved
    declaration entry for DeclRefExpr-based destinations (carrying
    initialization facts), else None.
    """
    kind = arg.get('kind')
    if kind == 'UnaryOperator' and arg.get('opcode') == '&':
        inner = strip_casts((arg.get('inner') or [{}])[0])
        sub = inner.get('kind')
        qual = (inner.get('type') or {}).get('qualType', '')
        flavor = dest_flavor(qual)
        decl = None
        if sub == 'DeclRefExpr':
            decl = collector.resolve_decl(inner)
            ref_kind = decl.get('kind', '')
            storage = decl.get('storageClass')
            if ref_kind == 'ParmVarDecl':
                base = 'ADDR-PARAM'
            elif storage == 'static':
                base = ('ADDR-STATIC-LOCAL'
                        if collector.inside_function(decl) else 'ADDR-GLOBAL')
            elif storage == 'extern' or not collector.inside_function(decl):
                base = 'ADDR-GLOBAL'
            else:
                base = 'ADDR-LOCAL'
        elif sub == 'MemberExpr':
            base = 'ADDR-FIELD'
        elif sub == 'ArraySubscriptExpr':
            base = 'ADDR-ELEMENT'
        else:
            return ('OTHER', flavor, None)
        if flavor == 'PP':
            return ('NESTED-PP', flavor, decl)
        if flavor != 'SCALAR-PTR':
            return (base + '-AGG', flavor, decl)
        return (base, flavor, decl)
    if kind == 'ArraySubscriptExpr':
        qual = (arg.get('type') or {}).get('qualType', '')
        flavor = dest_flavor(qual)
        if flavor == 'SCALAR-PTR':
            return ('ADDR-ELEMENT', flavor, None)
        return ('AGG-WRITE', flavor or 'OTHER', None)
    qual = (arg.get('type') or {}).get('qualType', '')
    flavor = dest_flavor(qual)
    if flavor == 'PP':
        return ('BARE-PP', flavor, None)
    if flavor == 'SCALAR-PTR' or flavor == 'AGG':
        return ('PTR-ARG', flavor, None)
    return ('OTHER', flavor, None)


def row_bucket(shapes):
    """Derive the row bucket from the set of argument buckets."""
    shape_set = set(shapes)
    if 'NESTED-PP' in shape_set:
        return 'NESTED-PP'
    for bucket in PO_SHAPES:
        if bucket in shape_set:
            return bucket
    if any(s.endswith('-AGG') for s in shape_set) or 'AGG-WRITE' in shape_set:
        return 'AGG-WRITE'
    if 'PTR-ARG' in shape_set:
        return 'PTR-ARG'
    return 'OTHER'


def statement_span(source, line, col):
    """Character span of the statement containing (line, col).

    Scans from the previous statement terminator to the next one; the
    result is used only for the documented guard heuristic.
    """
    lines = source.split('\n')
    if not (1 <= line <= len(lines)):
        return ''
    offset = sum(len(l) + 1 for l in lines[:line - 1]) + (col or 1) - 1
    begin = 0
    for i in range(offset - 1, -1, -1):
        if source[i] in ';}':
            begin = i + 1
            break
    end = len(source)
    depth_guard = 0
    for i in range(offset, len(source)):
        c = source[i]
        if c == '(':
            depth_guard += 1
        elif c == ')':
            depth_guard -= 1
        elif c == ';' and depth_guard <= 0:
            end = i
            break
    return source[begin:end]


def classify_guard(source, line, col):
    """Return (idiom, operator class) for the statement containing the call.

    The operator class records which comparison shape the guard uses:
    'eq' (== / !=), 'rel' (<, >, <=, >=), 'truth' (bare condition),
    'other'.  Only 'eq' shapes are in the bounded rule's recognized
    condition set; the rest are fail-closed yield limits.
    """
    stmt = statement_span(source, line, col)
    compact = ' '.join(stmt.split())
    if re.match(r'^(if|while|switch|for)\s*\(', compact.strip()):
        op = 'truth'
        if re.search(r'[=!]=', compact):
            op = 'eq'
        elif re.search(r'<|>', compact):
            op = 'rel'
        return 'COND-GUARD', op
    m = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*[A-Za-z_][A-Za-z0-9_]*\s*\(', compact)
    if m:
        var = m.group(1)
        # look ahead up to three statements for a comparison of var
        lines = source.split('\n')
        window = '\n'.join(lines[line - 1:line + 14])
        cmp_re = re.search(re.escape(var) + r'\s*(==|!=|<|>|<=|>=)', window)
        if cmp_re:
            op = 'eq' if cmp_re.group(1) in ('==', '!=') else 'rel'
            return 'ASSIGN-CHECKED', op
        # Truthiness guard on the assigned result: `if (var)` /
        # `if (!var)` / `while (var)` — the implicit boolean polarity
        # comparison the bounded rule recognizes (plan §2.4 C3).
        if re.search(r'(?:if|while)\s*\(\s*!?' + re.escape(var) +
                     r'\s*\)', window):
            return 'ASSIGN-CHECKED', 'truth'
        return 'ASSIGN-UNCHECKED', 'truth'
    return 'UNCHECKED', 'truth'


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
                         args.define, args.std,
                         os.path.join(args.workdir, 'ast'), args.generated)
    report = json.load(open(os.path.join(args.workdir, 'report.json')))

    from pathlib import Path
    ast_dir = Path(os.path.join(args.workdir, 'ast'))

    collectors = {}
    sources = {}
    for rel in args.file:
        src = os.path.realpath(os.path.join(facts.root, rel))
        if src in facts.skipped:
            continue
        tree = ast_for(facts.root, rel, ast_dir,
                       args.include, args.define, args.std)
        if tree is None:
            continue
        collectors[src] = CallSiteCollector(facts.root, rel, tree)
        try:
            with open(src, encoding='utf-8', errors='replace') as fh:
                sources[src] = fh.read()
        except OSError:
            sources[src] = ''
        del tree

    bucket_rows = collections.Counter()
    bucket_guard = collections.defaultdict(collections.Counter)
    bucket_guard_op = collections.Counter()
    bucket_variadic = 0
    bucket_null_init = 0
    bucket_callee_resolved = 0
    addr_local_sites = set()          # (function, symbol, file, line)
    addr_local_null_init_sites = set()
    bucket_family = collections.defaultdict(collections.Counter)
    bucket_symbols = collections.defaultdict(set)
    addressable_symbols = collections.defaultdict(set)
    detail = []
    measured = 0

    for o in report['unsupported']:
        if o['kind'].split(':')[0] != PO_KIND:
            continue
        measured += 1
        loc = o['primary_location']
        src = os.path.realpath(loc['file'])
        line, col = loc.get('line'), loc.get('column')
        sym = facts.resolve_symbol(o)
        family = facts.classify_symbol(sym)
        base = (sym or '').split(':')[0].rsplit(':', 1)[-1] if sym else ''
        if base in VA_SYMBOLS or (sym or '') in VA_SYMBOLS:
            bucket, shapes, guard, fn = 'VA-BUILTIN', ['VA-BUILTIN'], None, None
        else:
            collector = collectors.get(src)
            site = None
            if collector is not None:
                if col is not None:
                    site = collector.sites.get((line, col))
                if site is None and line is not None:
                    for (l, c), s in collector.sites.items():
                        if l == line:
                            site = s
                            break
            if site is None:
                bucket, shapes, guard, op, fn = 'UNMATCHED', ['UNMATCHED'], None, None, None
                variadic = False
                dest_null_init = None
            else:
                shapes = sorted({s for s, _ in site['args']} - {'OTHER'})
                if not shapes:
                    shapes = ['PTR-ARG'] if any(
                        s == 'PTR-ARG' for s, _ in site['args']) else ['OTHER']
                bucket = row_bucket(shapes)
                if bucket == 'ADDR-LOCAL' and not site.get('direct'):
                    # Well-shaped destination but an indirect callee:
                    # no symbol to bind a contract to.
                    bucket = 'ADDR-LOCAL-INDIRECT'
                guard, op = classify_guard(sources.get(src, ''), line, col)
                fn = site.get('fn')
                variadic = bool(site.get('variadic'))
                decls = site.get('out_decls') or []
                dest_null_init = None
                if bucket == 'ADDR-LOCAL' and decls:
                    inits = [bool(d.get('null_init')) for d in decls]
                    dest_null_init = all(inits)
                if bucket == 'ADDR-LOCAL' and site.get('callee_resolved'):
                    bucket_callee_resolved += 1
        bucket_rows[bucket] += 1
        if guard is not None:
            bucket_guard[bucket][guard] += 1
            if bucket == 'ADDR-LOCAL':
                if guard not in ('UNCHECKED', 'ASSIGN-UNCHECKED'):
                    bucket_guard_op[op] += 1
                if variadic:
                    bucket_variadic += 1
                if dest_null_init:
                    bucket_null_init += 1
        if bucket == 'ADDR-LOCAL':
            site_key = (fn, sym, loc['file'], line)
            addr_local_sites.add(site_key)
            if dest_null_init:
                addr_local_null_init_sites.add(site_key)
        bucket_family[bucket][family] += 1
        bucket_symbols[bucket].add(sym)
        if bucket == 'ADDR-LOCAL':
            addressable_symbols[family].add(sym)
        detail.append({
            'file': loc['file'], 'line': line, 'symbol': sym,
            'family': family, 'bucket': bucket, 'shapes': shapes,
            'guard': guard, 'guard_op': op, 'variadic_callee': variadic,
            'dest_null_init': dest_null_init, 'function': fn,
        })

    result = {
        'project': args.project,
        'measured_rows': measured,
        'bucket_rows': dict(bucket_rows),
        'bucket_guard': {k: dict(v) for k, v in bucket_guard.items()},
        'addr_local_guard_ops': dict(bucket_guard_op),
        'addr_local_variadic_callee_rows': bucket_variadic,
        'addr_local_callee_resolved_rows': bucket_callee_resolved,
        'addr_local_dest_null_init_rows': bucket_null_init,
        'addr_local_distinct_sites': len(addr_local_sites),
        'addr_local_distinct_null_init_sites': len(addr_local_null_init_sites),
        'bucket_family': {k: dict(v) for k, v in bucket_family.items()},
        'bucket_symbols': {k: sorted(v, key=lambda s: s or '')
                           for k, v in bucket_symbols.items()},
        'addressable_symbols': {k: sorted(v, key=lambda s: s or '') for k, v in
                                addressable_symbols.items()},
        'addressable_symbol_count': {k: len(v) for k, v in
                                     addressable_symbols.items()},
        'row_doc': ROW_DOC,
        'guard_doc': GUARD_DOC,
        'rows': detail,
    }
    if args.json_out:
        with open(args.json_out, 'w', encoding='utf-8') as fh:
            json.dump(result, fh, indent=1, sort_keys=True)

    print(f'project {args.project}: {measured} {PO_KIND} rows')
    for bucket, count in sorted(bucket_rows.items(), key=lambda kv: -kv[1]):
        fam = ', '.join(f'{k}={v}' for k, v in
                        sorted(bucket_family[bucket].items(),
                               key=lambda kv: -kv[1]))
        print(f'  {bucket:17s} {count:6d}   [{fam}]')
    total_local = bucket_rows.get('ADDR-LOCAL', 0)
    print(f'  ADDR-LOCAL guard idioms: '
          f'{dict(bucket_guard.get("ADDR-LOCAL", {}))}')
    print(f'  ADDR-LOCAL guard operators: {dict(bucket_guard_op)}')
    print(f'  ADDR-LOCAL variadic-callee rows: {bucket_variadic} '
          f'(callee decl resolved for {bucket_callee_resolved} rows); '
          f'null-initialized destination rows: {bucket_null_init}')
    print(f'  ADDR-LOCAL distinct sites: {len(addr_local_sites)} '
          f'({len(addr_local_null_init_sites)} null-initialized)')
    print(f'  ADDR-LOCAL distinct callees by family: '
          f'{ {k: len(v) for k, v in addressable_symbols.items()} }')
    print(f'  bounded-rule yield ceiling: {total_local}/{measured} rows')
    return 0


if __name__ == '__main__':
    sys.exit(main())
