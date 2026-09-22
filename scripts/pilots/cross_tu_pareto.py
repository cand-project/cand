#!/usr/bin/env python3
"""Cross-TU adoption Pareto measurement (milestone #42 Gate A evidence).

Measures, for one pinned project, the same-project cross-translation-unit
blocker family alongside the other blocker families, using the same
callee-resolved attribution method as
``docs/pilots/EXTERNAL-API-BOUNDARY-PARETO.md`` (external boundary
milestone #58), and classifies every XTU-blocked callee by the summary
shape the verifier itself derives for that callee's body in its own TU.

Method
------
1. Every measured TU is dumped to clang AST JSON (flag-keyed cache; a dump
   is only cached when clang exits 0, so a partial dump can never be
   reused) and analyzed for: function definitions and line ranges, call
   sites with best-effort direct callee names, per-TU same-file
   definitions, declaration signatures, and storage classes.
2. The verifier (``--contracts`` with the reviewed merged bundle) runs on
   the same file set.  The run is identical to an ordinary ``cand check``
   E1 measurement; ``CAND_DUMP_SUMMARIES`` may point a measurement build
   (verified obligation-identical to the stock binary) at a dump file so
   each TU's body-verified per-function summaries can be recorded.
3. Every obligation row is attributed to a blocking family:
   EXT (external API), XTU (project callee defined in another measured TU
   or elsewhere in the project), STU (same-TU callee whose body-derived
   summary is undecided), IND (indirect/callback call), ALIAS
   (alias/pointee-storage precision), OTHER.
4. Every project-local callee is classified into a cross-TU *shape* using
   the verifier's own derived summary for its definition plus AST facts
   (pointer-to-pointer parameters, function-pointer parameters, indirect
   calls in the body, recursion, generated-source definitions, declaration
   conflicts across TUs).

The script performs no semantic changes and draws no verdicts; it only
measures.  See ``docs/pilots/CROSS-TU-ADOPTION-PARETO.md``.
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

# External (non-project) symbol tables, shared with the #58 attribution
# tooling so family counts are comparable across milestones.
LIBC = {
    'malloc', 'calloc', 'realloc', 'free', 'memcpy', 'memmove', 'memset', 'memcmp', 'memchr',
    'strlen', 'strnlen', 'strchr', 'strrchr', 'strstr', 'strncmp', 'strcmp', 'strncpy', 'strcpy',
    'strcasecmp', 'strncasecmp', 'strdup', 'strndup', 'strtol', 'strtoul', 'atoi', 'atof',
    'snprintf', 'vsnprintf', 'sprintf', 'printf', 'fprintf', 'sscanf', 'abort', 'exit',
    'qsort', 'bsearch', 'abs', 'getenv', 'system', 'strerror', 'strtok', 'strtoll', 'strtoull',
    'llabs', 'labs', 'div', 'rand', 'srand', 'atexit', 'toupper', 'tolower', 'isalpha', 'isdigit',
    'isspace', 'isupper', 'islower', 'isprint', 'isxdigit', 'perror', 'strerror_r',
    'memcpy_s', 'strcpy_s', 'gmtime', 'localtime', 'mktime', 'time', 'clock', 'difftime',
    'qsort_r', 'aligned_alloc', 'at_quick_exit', 'quick_exit', 'strsignal', 'strspn', 'strcspn',
    'strpbrk', 'strcoll', 'strxfrm', 'wcscpy', 'wcslen', 'assert', 'pow', 'ceil', 'floor',
    'sqrt', 'fabs', 'log', 'log2', 'exp', 'fmod', 'isnan', 'isinf',
}
POSIX = {
    'connect', 'send', 'recv', 'sendto', 'recvfrom', 'socket', 'close', 'read', 'write',
    'setsockopt', 'getsockopt', 'bind', 'listen', 'accept', 'shutdown', 'fcntl', 'poll',
    'select', 'gettimeofday', 'usleep', 'sleep', 'getpid', 'kill', 'getaddrinfo',
    'freeaddrinfo', 'getnameinfo', 'gai_strerror', 'inet_ntop', 'inet_pton', 'htons',
    'htonl', 'ntohs', 'ntohl', 'pipe', 'dup', 'dup2', 'signal', 'sigaction', 'clock_gettime',
    'gethostname', 'uname', 'sysconf', 'mmap', 'munmap', 'mprotect', 'strerror_r',
    'pthread_mutex_lock', 'pthread_mutex_unlock', 'pthread_mutex_init', 'pthread_self',
    'getuid', 'geteuid', 'stat', 'fstat', 'lstat', 'open', 'creat', 'unlink', 'rename',
    'opendir', 'readdir', 'closedir', 'mkdir', 'access', 'ftruncate', 'lseek', 'fsync',
    'fdatasync', 'pread', 'pwrite', 'readlink', 'symlink', 'chmod', 'getcwd', 'realpath',
    'nanosleep', 'getrlimit', 'setrlimit', 'mlock', 'munlock', 'madvise', 'dlopen', 'dlclose',
    'dlsym', 'fopen', 'fclose', 'fwrite', 'fread', 'fflush', 'fseek', 'ftell', 'fgets',
    'fputs', 'fputc', 'getc', 'putc', 'ungetc', 'feof', 'ferror', 'remove', 'setvbuf',
    'tmpfile', 'rewind', 'gettimeofday', 'getpagesize', 'syscall',
}
BUILTIN = {
    '__errno_location', '__ctype_b_loc', '__ctype_tolower_loc', '__ctype_toupper_loc',
    '__builtin_va_start', '__builtin_va_end', '__builtin_va_copy', '__builtin_va_arg',
    '__stack_chk_fail', '__assert_fail', '__builtin_memcpy', '__builtin_memset',
    '__builtin_strchr', '__builtin_strlen', '__builtin_expect', '__builtin_abort',
    '__isoc99_sscanf', '__snprintf_chk', '__memcpy_chk', '__memset_chk', '__vsnprintf_chk',
    '__memcpy_chkre', '__fprintf_chk', '__printf_chk', '__sprintf_chk', '__fortify_fail',
}

# Project allocator macro aliases: the source-column fallback resolves the
# identifier at the obligation column, which for macro-wrapped calls is the
# macro name.  Map them to the project functions they expand to.  A value
# of 'indirect' marks macros that expand to calls through function pointers.
MACRO_ALIASES = {
    'hiredis': {'s_malloc': 'hi_malloc', 's_realloc': 'hi_realloc', 's_free': 'hi_free',
                's_strdup': 'hi_strdup',
                'va_start': '__builtin_va_start', 'va_end': '__builtin_va_end',
                'va_copy': '__builtin_va_copy'},
    'zlib': {},
    'sqlite': {},
    'curl': {},
    'libgit2': {},
}

# Macro prefixes that expand to indirect (function-pointer) calls; the
# callee cannot be resolved by name.  Mirrors the #58 attribution tool.
INDIRECT_MACRO_PREFIXES = {
    'hiredis': ('_EL_',),
    'zlib': (),
    'sqlite': (),
    'curl': (),
    'libgit2': (),
}

# Allocator-like and free-like project symbols per project, used ONLY to
# label the F (allocator wrapper) and G (destructor wrapper) shape subclasses
# of B (owned return) and E (destroy).  Labeling never feeds any trust
# decision; the semantic facts come from the verifier's own summaries.
ALLOC_LIKE = {
    'hiredis': {'hi_malloc', 'hi_calloc', 'hi_realloc', 'hi_strdup'},
    'zlib': {'zcalloc'},
    'sqlite': {'sqlite3Malloc', 'sqlite3MallocZero', 'sqlite3DbMallocZero',
               'sqlite3DbMallocRaw', 'sqlite3MallocRaw'},
    'curl': {'Curl_cmalloc', 'malloc_calloc'},
    'libgit2': {'git__malloc', 'git__calloc', 'git__strdup'},
}
FREE_LIKE = {
    'hiredis': {'hi_free'},
    'zlib': {'zcfree'},
    'sqlite': {'sqlite3_free', 'sqlite3FreeX', 'sqlite3DbFree'},
    'curl': {'Curl_cfree', 'Curl_safefree'},
    'libgit2': {'git__free'},
}

ALIAS_KINDS = {'ambiguous-alias-target', 'unresolved-pointee-storage'}
CALL_KINDS = {'unknown-call-with-tracked-pointer', 'unknown-pointer-return-ownership',
              'unknown-call-with-pointer-output'}

FUNC_DEF_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_ \t\*]+[ \t\*]([A-Za-z_][A-Za-z0-9_]*)\s*\(', re.M)

SHAPE_DOC = {
    'A': 'pure borrow / no ownership effect',
    'B': 'owned return',
    'C': 'borrowed return from parameter',
    'D': 'unconditional consume',
    'E': 'unconditional destroy',
    'F': 'allocator wrapper (B over a project allocator)',
    'G': 'destructor wrapper (E over a project free)',
    'H': 'conditional / unsupported body effect',
    'I': 'pointer-output effect',
    'J': 'callback / function-pointer parameter',
    'L': 'recursive / cyclic dependency',
    'M': 'declaration mismatch / inconsistent across TUs',
    'U': 'defined in project but outside the measured TUs',
    'O': 'generated / configuration-dependent definition',
    'N': 'interposable (external linkage; counted separately)',
    'K': 'indirect call (IND family; counted separately)',
}


def walk(node, visit):
    if isinstance(node, dict):
        visit(node)
        for value in node.values():
            walk(value, visit)
    elif isinstance(node, list):
        for value in node:
            walk(value, visit)


def callee_name(node):
    """Best-effort callee name from a CallExpr's callee child."""
    kids = node.get('inner') or []
    if not kids:
        return None
    k = kids[0]
    for _ in range(4):
        kind = k.get('kind')
        if kind in ('DeclRefExpr', 'UnresolvedLookupExpr'):
            return k.get('name') or (k.get('referencedDecl') or {}).get('name')
        if kind == 'MemberExpr':
            name = k.get('name') or k.get('written') or (k.get('referencedDecl') or {}).get('name')
            return ('member:' + name) if name else None
        if kind in ('ImplicitCastExpr', 'ParenExpr', 'CStyleCastExpr',
                    'UnaryOperator', 'ParenListExpr'):
            inner = k.get('inner') or []
            if not inner:
                return None
            k = inner[0]
            continue
        return None
    return None


class ProjectFacts:
    """AST-derived facts about the measured project."""

    def __init__(self, project, root, files, includes, defines, std, ast_dir, generated):
        self.project = project
        self.root = os.path.realpath(root)
        self.files = list(files)
        self.includes = list(includes)
        self.defines = list(defines)
        self.std = std
        self.generated = [os.path.realpath(g) if os.path.isabs(g)
                          else os.path.realpath(os.path.join(self.root, g)) for g in generated]
        self.byfile = collections.defaultdict(list)   # realpath file -> [(start,end,name)]
        self.allfns = set()
        self.calls = collections.defaultdict(dict)    # realpath file -> {(line,col): sym}
        self.same_tu_defs = {}                        # realpath file -> set(names)
        self.proj_defs = {}                           # name -> set of defining realpaths
        self.decl_types_tu = collections.defaultdict(
            lambda: collections.defaultdict(set))     # realpath file -> name -> {types}
        self.fn_params = {}                           # name -> [param type strings]
        self.static_defs = collections.defaultdict(set)  # name -> {files defining it static}
        self.extern_defs = collections.defaultdict(set)  # name -> {files defining it extern}
        self.measured_files = set()
        self.skipped = []
        self._collect(ast_dir)
        self._scan_project_tree()

    def _collect(self, ast_dir):
        from pathlib import Path
        ast_dir = Path(ast_dir)
        os.makedirs(ast_dir, exist_ok=True)
        for f in self.files:
            tree = ast_for(self.root, f, ast_dir, self.includes, self.defines, self.std)
            if tree is None:
                self.skipped.append(f)
                continue
            src = os.path.realpath(os.path.join(self.root, f))
            self.measured_files.add(src)
            for _, s, e, n in functions_of(self.root, f, tree, self.root):
                self.byfile[src].append((s, e, n))
                self.allfns.add(n)
            self.same_tu_defs[src] = {n for _, _, n in self.byfile[src]}
            self._collect_decls(tree, src)
            self._collect_calls(tree, src)
            # large projects: do not retain AST trees (vdbe.c-scale dumps)
            del tree
        for src in self.byfile:
            self.byfile[src].sort()

    def _collect_decls(self, tree, src):
        def visit(node):
            if node.get('kind') != 'FunctionDecl' or not node.get('name'):
                return
            name = node['name']
            qual = re.sub(r'\s+', ' ', node.get('type', {}).get('qualType', '') or '')
            if qual:
                self.decl_types_tu[src][name].add(qual)
            has_body = any(c.get('kind') == 'CompoundStmt' for c in node.get('inner') or [])
            if not has_body:
                return
            loc = node.get('loc', {})
            src_file = loc.get('file')
            if src_file:
                src_file = os.path.realpath(os.path.join(self.root, src_file))
            elif 'includedFrom' in loc:
                src_file = os.path.realpath(
                    os.path.join(self.root, loc['includedFrom'].get('file', '')))
            else:
                src_file = src
            if not src_file.startswith(self.root + os.sep):
                return
            self.proj_defs.setdefault(name, set()).add(src_file)
            params = []
            for child in node.get('inner') or []:
                if child.get('kind') == 'ParmVarDecl':
                    params.append(child.get('type', {}).get('qualType', '') or '')
            self.fn_params[name] = params
            if node.get('storageClass') == 'static':
                self.static_defs[name].add(src_file)
            else:
                self.extern_defs[name].add(src_file)
        walk(tree, visit)

    def _collect_calls(self, tree, src):
        def visit_calls(node, cur_line):
            if not isinstance(node, dict):
                return
            r = node.get('range', {}).get('begin', {})
            line = r.get('line', cur_line)
            if node.get('kind') == 'CallExpr':
                col = r.get('col') or r.get('column')
                if col is not None and line is not None:
                    self.calls[src][(line, col)] = callee_name(node)
            for child in node.get('inner') or []:
                visit_calls(child, line)
        visit_calls(tree, None)

    def _scan_project_tree(self):
        """Regex scan of project .c files outside the measured set so that
        callees defined elsewhere in the project are recognized as
        project-local (the closed universe is the project, not the sample)."""
        for dirpath, dirnames, filenames in os.walk(self.root):
            dirnames[:] = [d for d in dirnames
                           if d not in ('.git', 'node_modules') and not d.startswith('.')]
            for fn in filenames:
                if not fn.endswith('.c'):
                    continue
                path = os.path.realpath(os.path.join(dirpath, fn))
                if path in self.measured_files:
                    continue
                try:
                    text = open(path, errors='replace').read()
                except OSError:
                    continue
                for m in FUNC_DEF_RE.finditer(text):
                    self.proj_defs.setdefault(m.group(1), set()).add(path)

    def function_for(self, file, line):
        for s, e, n in self.byfile.get(os.path.realpath(file), []):
            if s <= line <= e:
                return n
        return None

    def is_generated(self, path):
        path = os.path.realpath(path)
        return any(path == g or path.startswith(g + os.sep) for g in self.generated)

    def classify_symbol(self, sym):
        """A = ISO C, B = POSIX/system/compiler runtime, C = external,
        D = project-local, E = indirect/unresolved."""
        if sym is None or sym == 'indirect':
            return 'E'
        if sym.startswith('member:'):
            return 'E'
        sym = MACRO_ALIASES.get(self.project, {}).get(sym, sym)
        if sym in self.proj_defs:
            return 'D'
        if sym in LIBC:
            return 'A'
        if sym in POSIX:
            return 'B'
        if sym in BUILTIN or sym.startswith('__builtin') or sym.startswith('__'):
            return 'B'
        return 'C'

    def resolve_symbol(self, o):
        kind = o['kind']
        if ':' in kind:
            return kind.split(':', 1)[1]
        loc = o['primary_location']
        key = (loc['line'], loc.get('column'))
        sym = self.calls.get(os.path.realpath(loc['file']), {}).get(key) or o.get('symbol')
        if sym is not None:
            return sym
        try:
            line = open(loc['file'], errors='replace').read().splitlines()[loc['line'] - 1]
        except (IndexError, OSError):
            return None
        col = (loc.get('column') or 1) - 1
        if 0 <= col < len(line):
            m = re.match(r'[A-Za-z_][A-Za-z0-9_]*', line[col:])
            if m:
                name = m.group(0)
                if name.startswith(INDIRECT_MACRO_PREFIXES.get(self.project, ())):
                    return 'indirect'
                return MACRO_ALIASES.get(self.project, {}).get(name, name)
            m2 = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*\($', line[:col + 1])
            if m2:
                return 'indirect'
        return None

    def row_family(self, o, sym):
        base = o['kind'].split(':')[0]
        if base not in CALL_KINDS:
            if base in ALIAS_KINDS:
                return 'ALIAS'
            return 'OTHER'
        suffixed = ':' in o['kind']
        cat = self.classify_symbol(sym)
        if cat == 'E':
            return 'IND'
        if cat in ('A', 'B', 'C'):
            return 'EXT'
        if suffixed:
            return 'XTU'
        loc = o['primary_location']
        tu_defs = self.same_tu_defs.get(os.path.realpath(loc['file']), set())
        if sym in tu_defs:
            return 'STU'
        return 'XTU'


def run_cand(files, includes, defines, contracts, out_path, err_path, std, dump_path):
    cmd = [CAND, "check", "--format", "json", "--contracts", contracts]
    cmd += [os.path.realpath(f) for f in files]
    cmd += ["--"]
    for inc in includes:
        cmd += ["-I", inc]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-std=" + std]
    env = dict(os.environ)
    if dump_path:
        env["CAND_DUMP_SUMMARIES"] = dump_path
    else:
        env.pop("CAND_DUMP_SUMMARIES", None)
    with open(out_path, "w") as out, open(err_path, "w") as err:
        return subprocess.run(cmd, stdout=out, stderr=err, env=env).returncode


def load_summaries(dump_path):
    """name -> {file -> [records]} from the measurement dump."""
    records = collections.defaultdict(lambda: collections.defaultdict(list))
    if not dump_path or not os.path.exists(dump_path):
        return records
    for line in open(dump_path):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        records[r['name']][r['file']].append(r)
    return records


def tarjan_scc(graph):
    """Iterative Tarjan; graph: node -> iterable of successors."""
    index_counter = [0]
    stack, on_stack = [], set()
    index, lowlink = {}, {}
    sccs = []

    for start in graph:
        if start in index:
            continue
        work = [(start, iter(graph.get(start, ())))]
        index[start] = lowlink[start] = index_counter[0]
        index_counter[0] += 1
        stack.append(start)
        on_stack.add(start)
        while work:
            node, it = work[-1]
            advanced = False
            for succ in it:
                if succ not in graph:
                    continue
                if succ not in index:
                    index[succ] = lowlink[succ] = index_counter[0]
                    index_counter[0] += 1
                    stack.append(succ)
                    on_stack.add(succ)
                    work.append((succ, iter(graph.get(succ, ()))))
                    advanced = True
                    break
                if succ in on_stack:
                    lowlink[node] = min(lowlink[node], index[succ])
            if advanced:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                lowlink[parent] = min(lowlink[parent], lowlink[node])
            if lowlink[node] == index[node]:
                scc = []
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    scc.append(w)
                    if w == node:
                        break
                sccs.append(scc)
    return sccs


def decided_positions(record):
    """A summary record is fully decided when no reachable position is
    Unknown and no conflict was recorded."""
    if record['conflict']:
        return False
    if record['return'] == 'unknown':
        return False
    return all(p != 'unknown' for p in record['params'])


def body_calls_allocator_or_free(name, facts, like_set):
    """Labeling-only check: does the callee's body (in a measured TU)
    directly call a project allocator / free-like symbol?"""
    for f in facts.proj_defs.get(name, []):
        if f not in facts.measured_files:
            continue
        for (line, col), sym in facts.calls.get(f, {}).items():
            if sym in like_set:
                for s, e, n in facts.byfile.get(f, []):
                    if n == name and s <= line <= e:
                        return True
    return False


def shape_of(name, record, facts, scc_of):
    """Classify one project-local callee into the cross-TU shape taxonomy.
    Semantic facts come from the verifier's own summary; AST facts
    (fn-ptr params, recursion) mark excluded classes; F/G are labeling-only
    subclasses of B/E."""
    params = facts.fn_params.get(name, [])
    fn_ptr = any('(*' in p for p in params)
    if not decided_positions(record):
        if record['conflict']:
            return 'H'
        ptr_ptr = any(re.sub(r'\s+', '', p).count('*') >= 2 and '(' not in p for p in params)
        return 'I' if ptr_ptr else 'H'
    ret = record['return']
    effects = set(record['params'])
    if fn_ptr:
        return 'J'
    if name in scc_of and scc_of[name] > 1:
        return 'L'
    if name in facts.ALLOC_LIKE or (
            ret == 'owned' and body_calls_allocator_or_free(name, facts, facts.ALLOC_LIKE)):
        return 'F'
    if ret == 'owned':
        return 'B'
    if 'destroys' in effects:
        if body_calls_allocator_or_free(name, facts, facts.FREE_LIKE) or name in facts.FREE_LIKE:
            return 'G'
        return 'E'
    if 'consumes' in effects:
        return 'D'
    if ret == 'borrow_from_arg':
        return 'C'
    if effects <= {'no_ownership_effect', 'borrow'}:
        return 'A'
    return 'H'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--file", action="append", required=True)
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--std", default="gnu11")
    ap.add_argument("--contracts", required=True)
    ap.add_argument("--generated", action="append", default=[],
                    help="project path (absolute or relative to root) holding generated sources")
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    os.makedirs(args.workdir, exist_ok=True)
    ast_dir = os.path.join(args.workdir, "ast")
    facts = ProjectFacts(args.project, args.root, args.file, args.include,
                         args.define, args.std, ast_dir, args.generated)
    facts.ALLOC_LIKE = ALLOC_LIKE.get(args.project, set())
    facts.FREE_LIKE = FREE_LIKE.get(args.project, set())
    files = [os.path.realpath(os.path.join(facts.root, f))
             for f in args.file if f not in facts.skipped]

    report_path = os.path.join(args.workdir, "report.json")
    err_path = os.path.join(args.workdir, "report.err")
    dump_path = os.path.join(args.workdir, "summaries.jsonl")
    if os.path.exists(dump_path):
        os.unlink(dump_path)
    rc = run_cand(files, args.include, args.define, os.path.realpath(args.contracts),
                  report_path, err_path, args.std, dump_path)
    report = json.load(open(report_path))
    summaries = load_summaries(dump_path)

    # ---------------------------------------------------------------- rows
    rows = []
    for o in report['unsupported']:
        loc = o['primary_location']
        sym = facts.resolve_symbol(o)
        rows.append({
            'kind': o['kind'], 'base': o['kind'].split(':')[0], 'symbol': sym,
            'cat': facts.classify_symbol(sym), 'fam': facts.row_family(o, sym),
            'fn': facts.function_for(loc['file'], loc['line']),
            'file': loc['file'], 'line': loc['line'], 'col': loc.get('column'),
        })

    fn_blockers = collections.defaultdict(set)
    fn_rows = collections.defaultdict(list)
    for r in rows:
        if r['fn']:
            fn_blockers[r['fn']].add(r['fam'])
            fn_rows[r['fn']].append(r)
    findings_fns = set()
    for f in report.get('findings', []):
        loc = f.get('primary_location', f.get('location', {}))
        fn = facts.function_for(loc['file'], loc.get('line'))
        if fn:
            findings_fns.add(fn)
    blocked = set(fn_blockers) | findings_fns
    clear = facts.allfns - blocked

    fam_obl = collections.Counter(r['fam'] for r in rows)
    fam_fns = collections.Counter()
    for fn, fams in fn_blockers.items():
        fam_fns.update(fams)
    sole = collections.Counter()
    for fn, fams in fn_blockers.items():
        if len(fams) == 1:
            sole[next(iter(fams))] += 1

    # ------------------------------------------------------- call graph / SCC
    call_graph = collections.defaultdict(set)
    for src, sites in facts.calls.items():
        for (line, col), sym in sites.items():
            if sym is None or sym.startswith('member:') or sym == 'indirect':
                continue
            sym = MACRO_ALIASES.get(args.project, {}).get(sym, sym)
            if sym not in facts.proj_defs:
                continue
            caller = facts.function_for(src, line)
            if caller:
                call_graph[caller].add(sym)
    sccs = tarjan_scc(call_graph)
    scc_of = {}
    for scc in sccs:
        for n in scc:
            scc_of[n] = len(scc)
    cyclic_groups = [s for s in sccs if len(s) > 1]
    self_recursive = {n for n in call_graph if n in call_graph[n]}

    # ------------------------------------------------------- XTU shape census
    xtu_syms = {r['symbol'] for r in rows if r['fam'] == 'XTU'}
    shape_counts = collections.Counter()
    sym_detail = {}
    inconsistent = set()
    for sym in sorted(xtu_syms):
        if sym is None:
            continue
        recs_by_file = summaries.get(sym, {})
        in_measured = any(f in facts.measured_files for f in facts.proj_defs.get(sym, []))
        generated_only = bool(facts.proj_defs.get(sym)) and all(
            facts.is_generated(f) for f in facts.proj_defs[sym])
        detail = {'symbol': sym,
                  'defs': sorted(os.path.relpath(f, facts.root)
                                 for f in facts.proj_defs.get(sym, [])),
                  'static': sym in facts.static_defs and sym not in facts.extern_defs,
                  'in_measured_tus': in_measured,
                  'generated_only': generated_only}
        distinct = {(r['return'], r.get('borrow_arg'), tuple(r['params']), r['conflict'])
                    for recs in recs_by_file.values() for r in recs}
        if len(distinct) > 1:
            inconsistent.add(sym)
            detail['shape'] = 'M'
        elif not recs_by_file:
            detail['shape'] = 'U' if not generated_only else 'O'
        else:
            record = next(iter(next(iter(recs_by_file.values()))))
            detail['shape'] = shape_of(sym, record, facts, scc_of)
            detail['summary'] = {'return': record['return'],
                                 'borrow_arg': record.get('borrow_arg'),
                                 'params': record['params'],
                                 'conflict': record['conflict']}
        shape_counts[detail['shape']] += 1
        sym_detail[sym] = detail

    # ------------------------------------------------- counterfactual estimate
    def row_clearable(r):
        """Would this XTU obligation row clear under a merged summary store,
        given only already-representable effects?  Conservative."""
        sym = r['symbol']
        if sym is None or sym in inconsistent:
            return False
        recs_by_file = summaries.get(sym, {})
        if not recs_by_file:
            return False
        if sym not in facts.proj_defs:
            return False
        record = next(iter(next(iter(recs_by_file.values()))))
        if not decided_positions(record):
            return False
        # pointer-output rows need effects outside the first bounded set
        if r['base'] == 'unknown-call-with-pointer-output':
            return False
        # BorrowFromArg whose source argument is untracked keeps its
        # obligation (fail-closed); the caller argument expression is not
        # visible here, so BorrowFromArg counts as clearable and the
        # residual class is reported separately in the document.
        return True

    xtu_sole = {fn for fn, fams in fn_blockers.items() if fams == {'XTU'}}
    predicted_clear = {fn for fn in xtu_sole
                       if fn_rows[fn] and all(row_clearable(r) for r in fn_rows[fn])}
    predicted_clear_measured = {
        fn for fn in predicted_clear
        if all(any(f in facts.measured_files for f in facts.proj_defs.get(r['symbol'], []))
               for r in fn_rows[fn])}

    # ------------------------------------------- declaration / identity hazards
    tu_conflicts = sorted({(os.path.basename(f), n)
                           for f, names in facts.decl_types_tu.items()
                           for n, t in names.items() if len(t) > 1})
    extern_type_conflicts = sorted({
        n for n, files in facts.extern_defs.items()
        if len({next(iter(facts.decl_types_tu[f][n])) for f in files
                if facts.decl_types_tu[f].get(n)}) > 1})
    static_name_collisions = sorted(
        n for n, files in facts.static_defs.items() if len(files) > 1)

    result = {
        'project': args.project,
        'root': facts.root,
        'files': len(args.file),
        'files_skipped': facts.skipped,
        'cand_rc': rc,
        'functions': len(facts.allfns),
        'clear': len(clear),
        'blocked': len(blocked),
        'findings': len(report.get('findings', [])),
        'obligations': len(rows),
        'family_obligations': dict(fam_obl),
        'family_functions': dict(fam_fns),
        'sole_blockers': dict(sole),
        'xtu': {
            'obligations': fam_obl.get('XTU', 0),
            'functions': fam_fns.get('XTU', 0),
            'sole_blocked': sole.get('XTU', 0),
            'unique_callees': len(xtu_syms),
            'shapes': dict(shape_counts),
            'inconsistent_summary_symbols': sorted(inconsistent),
            'predicted_clear_strict': len(predicted_clear_measured),
            'predicted_clear_loose': len(predicted_clear),
            'predicted_clear_functions': sorted(predicted_clear),
            'callees_outside_measured_tus': sorted(
                s for s in xtu_syms
                if s and s in facts.proj_defs and
                not any(f in facts.measured_files for f in facts.proj_defs[s])),
        },
        'callgraph': {
            'direct_calls_to_project_symbols': sum(len(v) for v in call_graph.values()),
            'cyclic_groups': len(cyclic_groups),
            'functions_in_cycles': sum(len(s) for s in cyclic_groups),
            'self_recursive': len(self_recursive),
        },
        'declarations': {
            'same_tu_conflicts': [f"{f}:{n}" for f, n in tu_conflicts],
            'extern_type_conflicts': extern_type_conflicts,
            'static_name_collisions': static_name_collisions,
        },
        'indirect': {
            'obligations': fam_obl.get('IND', 0),
            'member_call_sites': sum(
                1 for sites in facts.calls.values()
                for sym in sites.values() if sym and sym.startswith('member:')),
            'functions_with_fnptr_params': sum(
                1 for n in facts.allfns if any('(*' in p for p in facts.fn_params.get(n, []))),
        },
        'interposition': {
            'non_static_xtu_callees': sorted(
                s for s in xtu_syms if s and s not in facts.static_defs),
            'static_xtu_callees': sorted(
                s for s in xtu_syms if s and s in facts.static_defs),
        },
        'generated_boundary': {
            'paths': args.generated,
            'xtu_callees_defined_only_in_generated': sorted(
                s for s, d in sym_detail.items() if d.get('generated_only')),
        },
        'unresolved_symbols': sorted(
            {r['symbol'] for r in rows
             if r['base'] in CALL_KINDS and r['symbol']
             and facts.classify_symbol(r['symbol']) == 'C'}),
        'shape_symbols': sym_detail,
    }
    print(json.dumps(result, indent=2))
    if args.json_out:
        json.dump(result, open(args.json_out, 'w'), indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
