#!/usr/bin/env python3
"""Real-world E2 measurement runner (issue #36 close-out).

Runs the C& verifier over one pinned project scope under a set of contract
configurations (B0 = bare, E2 = reviewed boundary library, E2+ = library +
draft pointer-output bundle) and produces the E2 measurement data as one
deterministic JSON document.

Method (extends scripts/pilots/external_boundary_experiment.py; see
docs/pilots/CAND1-REAL-WORLD-BASELINE.md for the original baseline this
must stay comparable with):

- one whole-corpus `cand check --level=cand1 --profile=semantic
  --format=json` run per configuration (cwd = project root), plus one
  single-file run per translation unit with the same configuration for
  per-TU verdicts, wall time, and peak RSS;
- per-TU verdicts come from cand exit codes (mapping verified empirically,
  see EXIT_CODE_VERDICT below); anything outside the expected verdict
  codes is classified as a tool error and recorded, never silently
  dropped;
- LOC accounting is the original baseline's method: per-file physical
  lines via `wc -l`, summed into verdict buckets by per-TU verdict class;
- the function universe comes from Clang AST main-file definition ranges
  (imported from external_boundary_experiment.py: ast_for/functions_of);
  it must equal cand's own coverage.functions_analyzed for
  the corpus run, and any mismatch is recorded prominently, not hidden.
  Macro-expansion-location definitions (start line 0: curl's
  curl_easy_setopt_err_* typecheck helpers, libgit2's git_hashmap/
  git_hashset template instantiations) are excluded from the universe —
  cand does not count them as analyzed functions and their 0-0 ranges
  cannot contain rows, so keeping them would only inflate CLEAR.
  File-local macro instantiations (libgit2 instantiates git_hashmap
  per file: git_attr_cache_filemap_* etc.) are counted by cand but
  have no real source ranges either, so the AST method cannot
  attribute them; obligations cand reports inside them land at the
  macro invocation line, outside every real function range, and are
  counted per config as unmapped_obligations. Excluding all of these
  from the CLEAR universe is the conservative direction: an excluded
  function can never be counted CLEAR. The universe check therefore
  compares real-range functions against cand's functions_analyzed and
  records any residual mismatch prominently.
  Functions are keyed by (file, name): distinct static functions that
  share a name across files are separate functions (cand counts them
  per TU), so a name-level metric would merge rows from unrelated
  bodies and make the universe check incomparable;
- a function is CLEAR when no obligation and no finding maps inside its
  body (range containment); gained/lost CLEAR sets and added/removed
  obligation sets are exact key-set diffs, never ad-hoc attribution;
- unsupported-obligation kinds are counted per base kind (the `:symbol`
  suffix stripped) and mapped through a fixed table to the 12 #36 cause
  slots (+ "other"); the table was built by enumerating the base kinds
  actually observed in real runs (hiredis B0/E2/E2+ corpus runs plus repo
  fixture sweeps under tests/) — no kind was invented, and kinds absent
  from the table fall to "other";
- regression guard (function-level soundness gate): every non-first
  configuration is compared against the first declared configuration; a
  violation is (a) a function that lost CLEAR, (b) an obligation added
  inside a function that was CLEAR in the baseline, or (c) a finding
  added inside a function that was CLEAR in the baseline. Obligations
  added at locations within already-blocked functions (for example rows
  surfaced when a contract forces tracking of a previously-untracked
  pointer, or kind upgrades at already-obligated locations) are reported
  in the pairwise delta but do not fail the guard.

Limits:

- wall time and peak RSS are inherently non-deterministic; they are
  stored under keys whose names end in "_nondeterministic" and are the
  only fields excluded from byte-for-byte run-to-run comparison;
- the cause-slot mapping is keyed on base kind only. In the current
  report format, realloc, union, and varargs evidence surfaces through
  `:symbol` suffixes (e.g. `unknown-pointer-return-ownership:realloc`)
  and the `mechanism` field rather than through distinct base kinds, so
  those slots can read 0 while related rows are visible in
  kind_counts_full; this is a documented limit of the fixed base-kind
  table, not an absence claim;
- at `--level=cand1` in non-agent JSON mode cand never exits 0: a clean
  TU reports exit 3 / "incomplete" (see the mapping note below). Per-TU
  verdicts therefore apply the agent-equivalent acceptance predicate
  (fail = findings; pass = no findings and no unsupported rows;
  incomplete = otherwise) to the parsed per-TU report. Equivalence of
  this predicate with the original baseline's per-TU invocation
  (--profile=generated --level=cand1 with a per-TU strict policy,
  CAND_TRUSTED_BASE_SHA-pinned scratch repository, no trusted
  contracts) was verified empirically on hiredis sockcompat.c (pass),
  alloc.c / read.c / sds.c (incomplete): machinery result and exit code
  matched the predicate on every TU. The raw exit-code class is kept in
  each per-TU entry for audit.

Usage:
  realworld_e2.py --project <name> --root <dir> (--file <src.c>)... \
      [--include <dir>]... [--define <D>]... [--std <gnu11|c11>] \
      (--bare-config <name> | --config <name>=<contracts.yaml>)... \
      [--config-flag <name>=<cand-flag>]... \
      [--no-regression-guard-for <name>]... \
      --json-out <file> [--workdir <dir>]

Cand and clang binaries come from $CAND_BIN / $CLANG_BIN (same
convention as external_boundary_experiment.py). The script writes cand
stdout JSON, stderr, per-TU reports, and /usr/bin/time samples under
<workdir> (default: $TMPDIR/cand-realworld-e2/<project>) and prints a
summary on stdout. Exit status: 0 = measurement completed and the
regression guard held; 1 = regression-guard violation; 2 = usage error.
A guard comparison that cannot be computed (corpus tool error on either
side) is recorded in the JSON and on stderr, not failed silently.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import shutil
import subprocess
import sys
import tempfile

# Reuse the AST main-file definition-range method verbatim from the
# method reference (ast_for / functions_of / states_for). The import is
# honest reuse: this script adds no new way to derive the universe.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import external_boundary_experiment as ext_exp  # noqa: E402

CAND = os.environ.get("CAND_BIN", "cand")
CLANG = os.environ.get("CLANG_BIN", "clang-18")

# Empirically verified exit-code -> verdict mapping.
#
# Verified on 2026-09-24 against /tmp/opencode/fresh-build/cand (current
# main) using repo fixtures with known verdicts:
#   tests/interprocedural/allocator_wrapper_safe.c  -> rc 0, "pass"   (default level)
#   tests/interprocedural/member_summary_uaf.c      -> rc 1, "fail"
#   tests/interprocedural/unknown_external.c        -> rc 3, "incomplete"
#   malformed/missing contract file                 -> rc 2 (usage/contract error)
#   nonexistent source file / bad CLI flag          -> rc 2
#   translation unit that does not compile          -> rc 2 ("translation unit
#     did not compile; no verdict is reported")
#   `cand policy diff` with weakened base policy    -> rc 4 (not used here)
#
# IMPORTANT cand1 caveat (src/cand.cpp, non-agent path): at
# `--level=cand1` with `--format=json` and no --agent, cand returns
# `hasFindings ? 1 : 3` and coerces the JSON "result" to "fail" or
# "incomplete" — exit 0 / "pass" is unreachable in this mode because
# C&1 acceptance requires trusted generated evidence and policy
# bindings. Clean TUs therefore show rc 3 with zero findings and zero
# unsupported rows; this harness additionally records a "clean" flag for
# those. Expected verdict codes are {0, 1, 3}; every other code (2, 4,
# ...) is classified as "tool-error".
EXIT_CODE_VERDICT = {0: "pass", 1: "fail", 3: "incomplete"}
VERDICT_CLASSES = ["pass", "fail", "incomplete", "tool-error"]

# The 12 #36 cause slots plus the residual "other" slot.
CAUSE_SLOTS = [
    "unknown external call",
    "cross-TU ownership effect",
    "callback/retention",
    "realloc",
    "pointer/integer provenance",
    "union",
    "aggregate transport",
    "varargs",
    "atomics",
    "nonlocal control flow",
    "compiler extension",
    "unsupported alias/storage",
    "other",
]

# Fixed base-kind -> cause-slot table. Every key below was observed in a
# real cand --level=cand1 run (hiredis B0/E2/E2+ corpus runs on
# /tmp/opencode/xtu/hiredis, plus sweeps of repo fixtures under
# tests/interprocedural, tests/storage, tests/p1, tests/p2,
# tests/cand1); nothing was invented. Base kinds not in this table map
# to "other" and are additionally recorded per config in
# "unmapped_kinds" so the mapping stays auditable.
#
# Notable assignments:
# - tracked-pointer-return is a function-boundary ownership leak through
#   an undeclared return -> "cross-TU ownership effect";
# - statement-expression (GCC statement expressions) and inline-asm are
#   compiler extensions;
# - realloc/union/varargs have no dedicated base kind in the current
#   report format (that evidence lives in `:symbol` suffixes and the
#   `mechanism` field), so those slots can read 0 — see module docstring.
KIND_TO_SLOT = {
    # observed in the hiredis B0/E2/E2+ corpus runs:
    "unknown-call-with-tracked-pointer": "unknown external call",
    "unknown-call-with-pointer-output": "unknown external call",
    "unknown-pointer-return-ownership": "unknown external call",
    "aggregate-copy-with-tracked-pointer": "aggregate transport",
    "aggregate-return-with-tracked-pointer": "aggregate transport",
    "ambiguous-alias-target": "unsupported alias/storage",
    "unresolved-pointee-storage": "unsupported alias/storage",
    "unmodelled-pointer-parameter": "unsupported alias/storage",
    "global-or-static-pointer-storage": "unsupported alias/storage",
    "stack-pointer-return": "unsupported alias/storage",
    "pointer-arithmetic-reassignment": "pointer/integer provenance",
    "tracked-owner-overwrite": "unsupported alias/storage",
    "tracked-pointer-return": "cross-TU ownership effect",
    "statement-expression": "compiler extension",
    "unrefined-out-owner-use": "other",
    # observed in repo fixture sweeps (same verifier, --level=cand1):
    "atomic-pointer-storage": "atomics",
    "nonlocal-control-flow": "nonlocal control flow",
    "pointer-integer-provenance": "pointer/integer provenance",
    "inline-asm": "compiler extension",
    "unknown-call-borrow-retention": "callback/retention",
    "allocation-to-untracked-storage": "unsupported alias/storage",
    "dynamic-array-storage": "unsupported alias/storage",
    "free-untracked-pointer": "unsupported alias/storage",
    "free-untracked-expression": "unsupported alias/storage",
    "destroy-untracked-pointer": "unsupported alias/storage",
    "contract-body-conflict": "other",
    "unreviewed-declaration-annotation": "other",
    "conflicting-declaration-annotation": "other",
    "standalone-move": "other",
    "move-to-non-consuming-parameter": "other",
    "transfer-untracked-pointer": "other",
}

# /usr/bin/time -f '%e %M' gives wall seconds and peak RSS in KiB.
TIME_BIN = "/usr/bin/time"


class ConfigAction(argparse.Action):
    """Append (name, contracts_or_None) while preserving declaration order
    across --bare-config and --config."""

    def __call__(self, parser, namespace, values, option_string=None):
        configs = getattr(namespace, "configs", None) or []
        if option_string == "--config":
            if "=" not in values:
                parser.error(f"--config expects <name>=<contracts.yaml>, got {values!r}")
            name, contracts = values.split("=", 1)
            if not os.path.isfile(contracts):
                parser.error(f"--config {name}: contracts file not found: {contracts}")
            contracts = os.path.realpath(contracts)
        else:
            name, contracts = values, None
        if any(n == name for n, _ in configs):
            parser.error(f"duplicate config name: {name}")
        configs.append((name, contracts))
        namespace.configs = configs


def verdict_of(rc: int) -> str:
    return EXIT_CODE_VERDICT.get(rc, "tool-error")


def base_kind(kind: str) -> str:
    """Strip the `:symbol` suffix (callee name / indirect / storage tag)."""
    return kind.split(":", 1)[0]


def obligation_keys(report: dict) -> set:
    keys = set()
    for o in report.get("unsupported", []):
        loc = o.get("primary_location", {})
        keys.add((loc.get("file", "?"), loc.get("line", 0), o.get("kind", "?")))
    return keys


def finding_keys(report: dict) -> list:
    out = []
    for f in report.get("findings", []):
        loc = f.get("primary_location", f.get("location", {}))
        out.append({"file": loc.get("file", "?"), "line": loc.get("line", 0),
                    "id": f.get("id", "?")})
    return sorted(out, key=lambda d: (d["file"], d["line"], d["id"]))


def cand_cmd(files, includes, defines, std, contracts=None, extra_flags=()):
    cmd = [CAND, "check", "--level=cand1", "--profile=semantic", "--format=json"]
    if contracts:
        cmd += ["--contracts", contracts]
    cmd += list(extra_flags)
    cmd += list(files)
    cmd += ["--"]
    for inc in includes:
        cmd += ["-I", inc]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-std=" + std]
    return cmd


def run_corpus(cmd, cwd, out_path, err_path):
    """Whole-corpus run; returns (rc, wall_time_s, parsed_json_or_None, error)."""
    import time as _time
    t0 = _time.monotonic()
    with open(out_path, "w") as out, open(err_path, "w") as err:
        rc = subprocess.run(cmd, cwd=cwd, stdout=out, stderr=err).returncode
    wall = _time.monotonic() - t0
    report, error = None, None
    try:
        report = json.loads(open(out_path).read())
    except Exception as exc:
        error = f"cand stdout is not parseable JSON: {exc}"
    if rc not in EXIT_CODE_VERDICT:
        errnote = open(err_path, errors="replace").read().strip().splitlines()
        detail = errnote[-1] if errnote else ""
        error = (error or "") + f"; unexpected cand exit code {rc}"
        if detail:
            error += f": {detail}"
    return rc, wall, report, (error.strip("; ") if error else None)


def measure_child(cmd, cwd, time_path, out_path, err_path):
    """Run cmd under /usr/bin/time (or the getrusage fallback); returns
    (rc, wall_time_s, peak_rss_kb, method, caveat)."""
    import time as _time
    if os.path.isfile(TIME_BIN) and os.access(TIME_BIN, os.X_OK):
        full = [TIME_BIN, "-f", "%e %M", "-o", time_path] + cmd
        t0 = _time.monotonic()
        with open(out_path, "w") as out, open(err_path, "w") as err:
            rc = subprocess.run(full, cwd=cwd, stdout=out, stderr=err).returncode
        wall = _time.monotonic() - t0
        rss = None
        try:
            # GNU time may prepend diagnostics such as
            # "Command exited with non-zero status 3" to the -o file;
            # the '%e %M' sample is always the last line.
            parts = open(time_path).read().split()
            wall, rss = float(parts[-2]), int(parts[-1])
        except Exception:
            pass
        caveat = None
        if rc in (126, 127):
            caveat = f"/usr/bin/time could not run cand (exit {rc})"
        method = "/usr/bin/time -f '%e %M'"
        return rc, wall, rss, method, caveat
    # Fallback: resource.getrusage(RUSAGE_CHILDREN) deltas. LIMITATION:
    # ru_maxrss for RUSAGE_CHILDREN is a high-water mark across *all*
    # reaped children, so a delta is an upper bound that can overestimate
    # any child using less RSS than an earlier one; wall time is
    # monotonic-clock based. Only used when /usr/bin/time is missing.
    import resource
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = _time.monotonic()
    with open(out_path, "w") as out, open(err_path, "w") as err:
        rc = subprocess.run(cmd, cwd=cwd, stdout=out, stderr=err).returncode
    wall = _time.monotonic() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    rss = max(0, after.ru_maxrss - before.ru_maxrss)
    caveat = ("getrusage(RUSAGE_CHILDREN) fallback: peak RSS is an "
              "upper-bound high-water delta, not a per-process maximum")
    return rc, wall, rss, "resource.getrusage(RUSAGE_CHILDREN)", caveat


def loc_of(path: str) -> int:
    """Physical lines exactly as the original baseline counted them
    (`wc -l`, i.e. newline count)."""
    out = subprocess.run(["wc", "-l", str(path)], capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"wc -l failed for {path}: {out.stderr.strip()}")
    return int(out.stdout.split()[0])


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--file", action="append", required=True)
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--std", choices=["gnu11", "c11"], default="gnu11")
    ap.add_argument("--bare-config", action=ConfigAction, dest="configs",
                    metavar="NAME", help="config with NO contracts")
    ap.add_argument("--config", action=ConfigAction, dest="configs",
                    metavar="NAME=CONTRACTS.YAML",
                    help="config with a contracts file")
    ap.add_argument("--config-flag", action="append", default=[],
                    metavar="NAME=CAND-FLAG",
                    help="extra cand CLI flag for that config (repeatable)")
    ap.add_argument("--no-regression-guard-for", action="append", default=[],
                    metavar="NAME")
    ap.add_argument("--json-out", required=True)
    ap.add_argument("--workdir")
    args = ap.parse_args()

    if not args.configs:
        ap.error("at least one --bare-config or --config is required")

    config_flags = collections.defaultdict(list)
    for spec in args.config_flag:
        if "=" not in spec:
            ap.error(f"--config-flag expects <name>=<cand-flag>, got {spec!r}")
        name, flag = spec.split("=", 1)
        if not any(n == name for n, _ in args.configs):
            ap.error(f"--config-flag names unknown config {name!r}")
        config_flags[name].append(flag)

    root = os.path.realpath(args.root)
    for f in args.file:
        if not os.path.isfile(os.path.join(root, f)):
            ap.error(f"file not found under root: {f}")

    workdir = args.workdir or os.path.join(
        tempfile.gettempdir(), "cand-realworld-e2", args.project)
    os.makedirs(workdir, exist_ok=True)
    ast_dir = os.path.join(workdir, "ast")
    os.makedirs(ast_dir, exist_ok=True)
    from pathlib import Path

    files = list(args.file)
    loc_per_file = {f: loc_of(os.path.join(root, f)) for f in files}
    loc_total = sum(loc_per_file.values())

    # ---- function universe (AST main-file definition ranges) ----------
    byfile = collections.defaultdict(list)
    allfns = set()

    def fn_key(src, name):
        """Canonical function key: (file, name) rendered as a string."""
        return f"{src}::{name}"

    skipped_ast = []
    for f in files:
        tree = ext_exp.ast_for(root, f, Path(ast_dir), args.include,
                               args.define, args.std)
        if tree is None:
            skipped_ast.append(f)
            continue
        for src, s, e, n in ext_exp.functions_of(root, f, tree, root):
            # Skip macro-expansion-location definitions (start line 0).
            # These are macro-generated helpers instantiated into the TU
            # (curl's curl_easy_setopt_err_* typecheck functions, libgit2's
            # git_hashmap/git_hashset template instantiations): cand does
            # not count them as analyzed functions, and their 0-0 ranges
            # cannot contain rows — keeping them only pollutes the
            # universe count and the CLEAR list.
            if s == 0:
                continue
            byfile[src].append((s, e, n))
            # Key functions by (file, name): distinct static functions
            # that share a name across files are separate functions
            # (cand's coverage.functions_analyzed counts them per TU),
            # and a name-level metric would merge them.
            allfns.add(fn_key(src, n))
    for f in byfile:
        byfile[f].sort()

    def containing_fn(path, line):
        """(file, name) key of the function containing (path, line), or None."""
        if not path:
            return None
        fp = os.path.realpath(path)
        for s, e, n in byfile.get(fp, ()):
            if s <= line <= e:
                return fn_key(fp, n)
        return None

    def states_for_pairs(corpus_json_path):
        """CLEAR/BLOCKED per (file, function), same range-containment
        method as external_boundary_experiment.states_for but keyed on
        (file, name): distinct static functions sharing a name across
        files are separate functions, and a name-level metric would
        merge rows from unrelated bodies."""
        d = json.load(open(corpus_json_path))
        blocked = set()
        unmapped = 0
        for o in d["unsupported"]:
            loc = o["primary_location"]
            fp = os.path.realpath(loc["file"])
            hit = None
            for s, e, n in byfile.get(fp, []):
                if s <= loc["line"] <= e:
                    hit = fn_key(fp, n)
                    break
            if hit:
                blocked.add(hit)
            else:
                unmapped += 1
        for f in d.get("findings", []):
            loc = f.get("primary_location", f.get("location", {}))
            fp = os.path.realpath(loc["file"])
            for s, e, n in byfile.get(fp, []):
                if s <= loc["line"] <= e:
                    blocked.add(fn_key(fp, n))
                    break
        return d, allfns - blocked, blocked, unmapped

    time_method = None
    time_caveat = None
    per_config = {}
    universe_check = {}

    for name, contracts in args.configs:
        cdir = os.path.join(workdir, "configs", name)
        os.makedirs(cdir, exist_ok=True)
        flags = config_flags.get(name, [])

        # ---- whole-corpus run -----------------------------------------
        corpus_json = os.path.join(cdir, "corpus.json")
        corpus_err = os.path.join(cdir, "corpus.err")
        cmd = cand_cmd(files, args.include, args.define, args.std,
                       contracts, flags)
        rc, wall, report, tool_error = run_corpus(
            cmd, root, corpus_json, corpus_err)

        entry = {
            "contracts": contracts,
            "extra_flags": sorted(flags),
            "corpus_command": cmd,
            "corpus_exit_code": rc,
            "corpus_verdict": verdict_of(rc),
            "corpus_json_path": corpus_json,
            "corpus_stderr_path": corpus_err,
            "corpus_tool_error": tool_error,
            "corpus_wall_time_s_nondeterministic": round(wall, 3),
        }

        if report is not None:
            entry["corpus_result"] = report.get("result")
            cov = report.get("coverage", {}) or {}
            entry["functions_analyzed"] = cov.get("functions_analyzed")
            entry["findings_count"] = len(report.get("findings", []))
            entry["findings"] = finding_keys(report)
            entry["obligations_count"] = len(report.get("unsupported", []))
            oblig_keys = obligation_keys(report)
            entry["obligation_keys"] = sorted(
                f"{f}:{line}:{kind}" for f, line, kind in oblig_keys)
            kind_counts = collections.Counter()
            kind_counts_full = collections.Counter()
            slot_counts = collections.Counter()
            unmapped = set()
            for o in report.get("unsupported", []):
                kind = o.get("kind", "?")
                kind_counts_full[kind] += 1
                bk = base_kind(kind)
                kind_counts[bk] += 1
                slot = KIND_TO_SLOT.get(bk)
                if slot is None:
                    unmapped.add(bk)
                    slot = "other"
                slot_counts[slot] += 1
            entry["kind_counts"] = dict(sorted(kind_counts.items()))
            entry["kind_counts_full"] = dict(sorted(kind_counts_full.items()))
            entry["cause_slot_counts"] = {s: slot_counts.get(s, 0)
                                          for s in CAUSE_SLOTS}
            entry["unmapped_kinds"] = sorted(unmapped)

            if allfns:
                _, clear, blocked, unmapped_ob = states_for_pairs(
                    corpus_json)
                entry["clear_functions"] = sorted(clear)
                entry["blocked_functions"] = sorted(blocked)
                entry["clear_count"] = len(clear)
                entry["blocked_count"] = len(blocked)
                entry["unmapped_obligations"] = unmapped_ob

            fa = cov.get("functions_analyzed")
            universe_check[name] = {
                "ast_function_universe": len(allfns),
                "cand_functions_analyzed": fa,
                "match": fa == len(allfns),
            }
        else:
            entry["corpus_result"] = None
            universe_check[name] = {
                "ast_function_universe": len(allfns),
                "cand_functions_analyzed": None,
                "match": None,
                "note": "corpus report unavailable (tool error)",
            }

        # ---- per-TU runs ------------------------------------------------
        per_tu = {}
        per_tu_meas = {}
        loc_buckets = {v: 0 for v in VERDICT_CLASSES}
        any_time_caveat = None
        for f in files:
            tu_json = os.path.join(cdir, "tu", f.replace("/", "_") + ".json")
            tu_err = tu_json + ".err"
            os.makedirs(os.path.dirname(tu_json), exist_ok=True)
            tu_cmd = cand_cmd([f], args.include, args.define, args.std,
                              contracts, flags)
            tu_rc, tu_wall, tu_rss, method, caveat = measure_child(
                tu_cmd, root, tu_json + ".time", tu_json, tu_err)
            time_method, any_time_caveat = method, (caveat or any_time_caveat)
            tu_report = None
            try:
                tu_report = json.loads(open(tu_json).read())
            except Exception:
                pass
            verdict = verdict_of(tu_rc)      # raw exit-code class
            if tu_report is not None and tu_rc in EXIT_CODE_VERDICT:
                # cand1 non-agent JSON mode coerces pass to exit 3, so the
                # raw exit code cannot carry the original baseline's PASS
                # notion. Derive the verdict from the agent-equivalent
                # acceptance predicate (fail = findings; pass = no findings
                # and no unsupported rows; incomplete = otherwise), whose
                # equivalence with the original generated-policy machinery
                # was verified empirically (see the method note above).
                if tu_report.get("findings"):
                    verdict = "fail"
                elif tu_report.get("unsupported"):
                    verdict = "incomplete"
                else:
                    verdict = "pass"
            tu_entry = {
                "exit_code": tu_rc,
                "verdict": verdict,
                "result": tu_report.get("result") if tu_report else None,
                "findings": len(tu_report.get("findings", [])) if tu_report else None,
                "obligations": len(tu_report.get("unsupported", [])) if tu_report else None,
                "loc": loc_per_file[f],
            }
            if tu_report is not None and tu_rc in EXIT_CODE_VERDICT:
                tu_entry["clean"] = (not tu_report.get("findings")
                                     and not tu_report.get("unsupported"))
            else:
                tu_entry["clean"] = False
                if tu_rc not in EXIT_CODE_VERDICT:
                    last = open(tu_err, errors="replace").read().strip().splitlines()
                    tu_entry["tool_error"] = (last[-1] if last
                                              else f"unexpected exit code {tu_rc}")
            per_tu[f] = tu_entry
            per_tu_meas[f] = {"wall_time_s": round(tu_wall, 3),
                              "peak_rss_kb": tu_rss}
            loc_buckets[verdict] += loc_per_file[f]

        entry["per_tu"] = per_tu
        entry["per_tu_loc_buckets"] = loc_buckets
        entry["loc_coverage_pct"] = round(
            100.0 * loc_buckets["pass"] / loc_total, 4) if loc_total else None
        entry["loc_decidable_pct"] = round(
            100.0 * (loc_buckets["pass"] + loc_buckets["fail"]) / loc_total,
            4) if loc_total else None
        entry["per_tu_runtime_rss_nondeterministic"] = per_tu_meas
        entry["measurement_method"] = {
            "runtime_rss": time_method,
            "runtime_rss_caveat": any_time_caveat,
        }
        per_config[name] = entry

    # ---- pairwise deltas (declaration order) ---------------------------
    names = [n for n, _ in args.configs]
    pairwise = {}
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            a, b = names[i], names[j]
            pair = f"{a}->{b}"
            ea, eb = per_config[a], per_config[b]
            missing = [n for n, e in ((a, ea), (b, eb))
                       if "obligation_keys" not in e or "clear_functions" not in e]
            if missing:
                pairwise[pair] = {"error": "corpus report unavailable for "
                                           f"{missing[0]} (tool error)"}
                continue
            ka = obligation_keys_from_entry(ea)
            kb = obligation_keys_from_entry(eb)
            ca = set(ea.get("clear_functions", []))
            cb = set(eb.get("clear_functions", []))
            # Location-aware split of added rows: an added obligation at a
            # (file, line) that already carried an obligation of any kind in
            # the baseline config is a precision reclassification (kind
            # upgrade) at already-blocked code, not newly-blocked code.
            locs_a = {(f, line) for f, line, _ in ka}
            added_all = sorted(f"{f}:{line}:{kind}" for f, line, kind in kb - ka)
            added_new_loc = sorted(
                f"{f}:{line}:{kind}" for f, line, kind in kb - ka
                if (f, line) not in locs_a)
            added_kind_upgrade = sorted(
                f"{f}:{line}:{kind}" for f, line, kind in kb - ka
                if (f, line) in locs_a)
            pairwise[pair] = {
                "obligations_added": added_all,
                "obligations_added_at_new_locations": added_new_loc,
                "obligations_added_at_existing_locations": added_kind_upgrade,
                "obligations_removed": sorted(
                    f"{f}:{line}:{kind}" for f, line, kind in ka - kb),
                "clear_gained": sorted(cb - ca),
                "clear_lost": sorted(ca - cb),
                "counts": {
                    "obligations_added": len(kb - ka),
                    "obligations_added_at_new_locations": len(added_new_loc),
                    "obligations_added_at_existing_locations": len(
                        added_kind_upgrade),
                    "obligations_removed": len(ka - kb),
                    "clear_gained": len(cb - ca),
                    "clear_lost": len(ca - cb),
                },
            }

    # ---- regression guard (soundness gate) ------------------------------
    baseline = names[0]
    exempt = set(args.no_regression_guard_for)
    unknown_exempts = exempt - set(names)
    if unknown_exempts:
        ap.error(f"--no-regression-guard-for names unknown configs: "
                 f"{sorted(unknown_exempts)}")
    violations = []
    not_computable = []
    baseline_clear = set(per_config[baseline].get("clear_functions", []))
    for name in names[1:]:
        if name in exempt:
            continue
        pair = pairwise.get(f"{baseline}->{name}", {})
        if "error" in pair:
            not_computable.append(name)
            continue
        eb = per_config[name]
        lost = pair.get("clear_lost", [])
        # Function-level soundness gate: the guard protects the CLEAR
        # metric. A violation is (a) a function that lost CLEAR, (b) an
        # obligation added inside a function that was CLEAR in the
        # baseline, or (c) a finding added inside a function that was
        # CLEAR in the baseline. Obligations added at new locations
        # within already-blocked functions (e.g. rows surfaced when a
        # contract forces tracking of a previously-untracked pointer)
        # are recorded in the pairwise delta but do not fail the guard.
        added = pair.get("obligations_added_at_new_locations", [])
        added_in_clear = []
        for row in added:
            path, line, _ = row.split(":", 2)
            if containing_fn(path, int(line)) in baseline_clear:
                added_in_clear.append(row)
        fkeys_a = {(fd.get("file"), fd.get("line"))
                   for fd in per_config[baseline].get("findings", [])}
        findings_in_clear = []
        for fd in eb.get("findings", []):
            if ((fd.get("file"), fd.get("line")) in fkeys_a):
                continue
            if containing_fn(fd.get("file"), fd.get("line")) in baseline_clear:
                findings_in_clear.append(
                    f"{fd.get('file')}:{fd.get('line')}:{fd.get('id')}")
        if lost or added_in_clear or findings_in_clear:
            violations.append({
                "config": name,
                "vs": baseline,
                "lost_clear": lost,
                "added_obligations_in_clear_functions": added_in_clear,
                "added_findings_in_clear_functions": findings_in_clear,
            })
    guard = {
        "baseline_config": baseline,
        "exempted_configs": sorted(exempt),
        "violations": violations,
        "not_computable": not_computable,
        "passed": not violations,
    }

    universe = {
        "count": len(allfns),
        "functions": sorted(allfns),
        "ast_skipped_files": sorted(skipped_ast),
        "per_config": universe_check,
    }
    matches = [v["match"] for v in universe_check.values() if v["match"] is not None]
    universe["all_match"] = all(matches) if matches else None
    if matches and not universe["all_match"]:
        # Prominent: surfaced at top level, not buried per config.
        universe["DISCREPANCY"] = (
            "AST function universe does not equal cand "
            "coverage.functions_analyzed for every config; see per_config")

    result = {
        "schema": "cand.realworld-e2/v1",
        "project": args.project,
        "root": root,
        "files": files,
        "loc_per_file": loc_per_file,
        "loc_total": loc_total,
        "cand_binary": shutil.which(CAND) or CAND,
        "clang_binary": shutil.which(CLANG) or CLANG,
        "std": args.std,
        "includes": list(args.include),
        "defines": list(args.define),
        "configs": names,
        "cause_slots": CAUSE_SLOTS,
        "cause_slot_mapping": dict(sorted(KIND_TO_SLOT.items())),
        "exit_code_verdict_mapping": dict(sorted(EXIT_CODE_VERDICT.items())),
        "function_universe": universe,
        "per_config": per_config,
        "pairwise_deltas": pairwise,
        "regression_guard": guard,
    }

    with open(args.json_out, "w") as out:
        json.dump(result, out, indent=2, sort_keys=True)
        out.write("\n")

    # ---- stdout summary -------------------------------------------------
    print(json.dumps({
        "project": args.project,
        "loc_total": loc_total,
        "function_universe": len(allfns),
        "universe_all_match": universe["all_match"],
        "configs": {
            n: {
                "corpus_result": per_config[n].get("corpus_result"),
                "corpus_verdict": per_config[n]["corpus_verdict"],
                "obligations": per_config[n].get("obligations_count"),
                "findings": per_config[n].get("findings_count"),
                "clear": per_config[n].get("clear_count"),
                "loc_buckets": per_config[n]["per_tu_loc_buckets"],
            } for n in names
        },
        "regression_guard_passed": guard["passed"],
        "violations": violations,
    }, indent=2, sort_keys=True))

    if violations:
        print("REGRESSION GUARD: soundness violation detected", file=sys.stderr)
        for v in violations:
            print(f"  config {v['config']} vs {v['vs']}: "
                  f"lost_clear={v['lost_clear']} "
                  f"added_obligations_in_clear_functions="
                  f"{v['added_obligations_in_clear_functions']} "
                  f"added_findings_in_clear_functions="
                  f"{v['added_findings_in_clear_functions']}", file=sys.stderr)
        return 1
    if not_computable:
        print(f"NOTE: regression guard not computable for {not_computable} "
              f"(corpus tool error); recorded, not failed", file=sys.stderr)
    return 0


def obligation_keys_from_entry(entry):
    """Recover the obligation key set from a per-config entry (or None).

    Keys are "<file>:<line>:<kind>" where the kind may itself contain
    colons (":symbol" suffixes); file paths contain none, so the parse
    splits from the LEFT. A malformed key is a data-integrity error and
    raises rather than being silently dropped.
    """
    if "obligation_keys" not in entry:
        return None
    keys = set()
    for s in entry["obligation_keys"]:
        f, line, kind = s.split(":", 2)
        line = int(line)  # raises on malformed keys: fail loud, not wrong
        keys.add((f, line, kind))
    return keys


if __name__ == "__main__":
    sys.exit(main())
