# ADR-0025 — Reassigned-Parameter Borrow-Origin Repair (Incident #62)

Status: accepted (2026-09-22, incident #62 repair)

## Context

`SummaryBuilder` derived a function's returned-pointer borrow origin from the
*syntactic* parameter reference at the return site (`borrowedParameterIndex`),
and mapped callee borrow-origins through *argument names* at the return call
(`parameterIndex`). Neither path recognized that a pointer parameter can be
**reassigned** in the body. The function

```c
static int *reassign(int *p, int *r) { p = r; return p; }
```

was therefore summarized `borrow_from_arg@0` while the returned pointer
aliases the object passed as **arg 1**.

Caller-side borrow invalidation keys on the summary's origin index. A caller
that destroyed the true origin (arg 1) left the returned borrow live, and a
subsequent use was a use-after-free with **no finding and no unsupported
obligation** — a semantic `pass` with nothing in `canEmitCand1Pass`'s semantic
conditions to block an authoritative generated-profile PASS. Incident #62
records the reproducer (ASan-confirmed heap-use-after-free at `-O0`), the
second confirmed shape (`p = r; return helper(p);`, the callee-mapping form),
and the fail-closed controls (every destroy/consume direction through a
reassigned parameter is held by the `CAND-O006` parameter-destruction guard;
the local-alias destroy direction is the documented false-FAIL defect of
issue #54). The defect was introduced with the borrow-summary machinery
(`bfc68a1`) and is present in the v0.2.0 and v0.2.1 releases.

Real-project extent (five-pilot summary diff, pre-fix vs post-fix): 7 functions
in hiredis `sds.c` (`sdscatlen`, `sdscpylen`, `sdsMakeRoomFor`,
`sdsRemoveFreeSpace`, `sdscatfmt`, `sdscatrepr`, `sdsgrowzero`), curl
`splay.c:splay`, and sqlite `window.c:exprListAppendList` (plus the functions
enumerated in the issue) all had the reassign-and-return shape; each such
summary claimed a borrow origin that was not the returned object.

## Decision

Fail closed on reassigned origins. When the parameter resolved as a return
borrow origin — directly through `borrowedParameterIndex(value, f)` or through
the return-call argument mapping `parameterIndex(call->getArg(*borrow), f)` —
is the target of **any assignment anywhere in the body**, the return effect is
`Unknown` instead of `BorrowFromArg` (or `Owned`). The existing
order-insensitive `assignedLater()` provides the any-assignment check and
already applies to `ParmVarDecl`.

The check is deliberately order-insensitive and shape-insensitive:

- a reassignment *after* the return statement also forces `Unknown`
  (conservative; dead-code reassignments cost precision, not soundness);
- compound assignments (`p += n`) are assignments and force `Unknown`, while
  `p++`/`p--` do not — they move within the same object, so the parameter
  still identifies the borrowed object at object granularity;
- the guard applies to the whole `borrowedParameterIndex` result, so
  `p->field` and `&p->field` origins through a reassigned `p` are also
  collapsed.

## Resulting semantics

- A body pattern that was incorrectly *decided* (with a wrong origin index)
  becomes honestly *undecided*: callers receive
  `unknown-pointer-return-ownership` obligations instead of a false borrow
  fact, and the verdict moves from PASS to INCOMPLETE.
- No new effect kinds, no lattice change, no annotation change, no
  contract-trust change: this is a soundness repair *inside* the existing
  summary semantics, not a scope expansion.
- Correct attributions are untouched: `return r;` after `p = r;` keeps
  `borrow_from_arg@1`, and non-reassigned borrow returns keep
  `borrow_from_arg` (regression pair in `tests/interprocedural/`).

## Evidence and regression

- Permanent paired fixtures:
  `tests/interprocedural/reassigned_borrow_origin_incomplete.c` (the incident
  reproducer; must be INCOMPLETE, never PASS),
  `reassigned_borrow_origin_helper_incomplete.c` (callee-mapping form),
  `reassigned_origin_direct_return_uaf.c` (correct-attribution control; must
  remain a FAIL detection — guards against over-collapsing).
- Full gate on the fix head: `scripts/check.sh`, `git diff --check`, complete
  CTest (20/20, including the C&1-E gate and sanitizer differential), CVE
  replay (all entries within recorded classifications).
- Five-pilot before/after: findings preserved except two curl `CAND-B003`
  borrow-escape findings in `splay.c` whose premise was `splay()`'s
  misattributed summary (the returned pointer is a different tree node, not
  the parameter's pointee) — the premise was the incident defect itself. All
  obligation movement is Unknown-ward: use-site obligations disappear only
  for pointers that became untracked unknown-origin values, and the
  corresponding creation sites gain `unknown-pointer-return-ownership`
  obligations, matching the pre-existing unknown-return semantics.
- zlib is byte-identical before and after (no reassign-and-return shape in
  the measured TUs).

## Claim impact

The public C&1/v1 claim of v0.2.0 and v0.2.1 is suspended/superseded
(`docs/SAFETY_CLAIMS.md`); claim restoration requires a post-incident release
with complete exact-head requalification per
`docs/CAND1-FALSE-PASS-RESPONSE.md`.
