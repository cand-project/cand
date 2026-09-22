# ADR-0026 — Compound-Origin Borrow-Resolution Repair (Incident #64)

Status: accepted (2026-09-22, incident #64 repair)

## Context

`SummaryBuilder`'s `borrowedParameterIndex(value, f)` resolved a returned
pointer's borrow origin from the return expression. After the direct
parameter reference and the `&p->member` form, its fallback returned the
**first parameter contained anywhere in the expression**:

```cpp
if (expr->getType()->isPointerType()) {
    for (unsigned i = 0; i < f.param_size(); ++i)
        if (containsParameter(expr, f, i)) return i;
}
```

This fallback ran *before* the return handler's `CallExpr` branch, so **any
pointer-typed return expression mentioning a parameter** was claimed as
`borrow_from_arg@<that parameter>` — regardless of what the expression
actually computes. "First parameter contained anywhere" is a syntactic
occurrence test, not an origin resolution. Confirmed false-PASS shapes
(ASan-confirmed heap-use-after-free at `-O0`; all `pass` with zero findings
and zero obligations on post-#63 main, shape 1 also on the immutable v0.2.1
tag binary):

1. **Conditional operator, non-parameter condition** —
   `int c = 0; return c ? a : b;` claimed `borrow_from_arg@0` (a); the
   runtime value is b's object.
2. **Conditional operator, pointer-parameter condition** —
   `return p ? a : b;` called with non-null `p` claimed `borrow_from_arg@0`
   (*p*, the condition); the runtime value is a's object.
3. **Comma operator with a parameter in the discarded side** —
   `return (first(a), b);` claimed `borrow_from_arg@0` (a); the value is b.
4. **Value read from parameter storage** — `return p[i];` on a `T**`
   parameter claimed a borrow of the parameter's pointee while the returned
   pointer targets a *different object* read out of that storage. The
   member form (`return p->f;` with pointer member `f`) is the same wrong
   fact and was held only incidentally (aliasing-write obligations at the
   probed caller shapes).

Adjacent family members that are wrong-as-fact but were fail-closed in the
probed shapes: an int parameter used as the conditional condition
(misattributed to the non-pointer parameter, blocked by
`unknown-pointer-return-ownership` at callers), `return *pp;` on a `T**`
parameter, parameter mixed with a global pointer, and the **call-arguments
containment form** — `return dupit(p);` was claimed as a borrow of `p`
before the callee-mapping branch could run, so wrappers of
fresh-allocating callees (hiredis `hi_malloc(size)` claiming a borrow of
its `size_t` parameter) carried invented borrow facts; at callers these
produced B003 artifacts and false-FAIL detections rather than false PASSes.

The defect was introduced with the borrow-summary machinery (`bfc68a1`) —
the same commit as incident #62 — and is present in the v0.2.0 and v0.2.1
releases. The #62 repair guards the *resolved* parameter against
reassignment but does not guard the resolution itself.

## Decision

Replace the containment fallback with a whitelist origin resolver
(`parameterDerivedOrigin`). An origin is resolved only when the return
expression is unambiguously derived, at object granularity, from exactly
one pointer parameter:

- **accepted**: direct references (`p`), member chains over non-pointer
  members (`p->s.arr`, array-member decay `p->arr`), interior addresses
  (`&p->f`, `&p[i]`), pointer arithmetic with an integer offset
  (`p + n`, `p - n`), same-parameter conditionals (`c ? p : p`,
  `c ? p : NULL`), and comma operators resolving on their last operand;
- **rejected** (no origin, return effect `Unknown`): conditional operators
  with distinct branch origins, parameters mixed with non-parameter
  pointers, pointer-typed member/subscript **value reads** (`p->f` with
  pointer member, `p[i]` on `T**`, `*pp`), `&p` (the parameter object
  itself), references to non-pointer parameters, member accesses on
  by-value parameters, and every `CallExpr` — which restores the
  callee-mapping branch (`returnEffect` + argument mapping) as the *only*
  path for call-shaped returns.

No new effect kinds, no lattice change, no annotation change, no
contract-trust change: like ADR-0025 this is a soundness repair inside the
existing summary semantics. The #62 reassignment guard applies unchanged to
whatever origin the resolver returns.

## Resulting semantics

- Wrongly *decided* summaries (wrong origin index, or an invented borrow
  for a non-borrow return) become honestly *undecided*: callers receive
  `unknown-pointer-return-ownership` obligations at creation sites and
  downstream uses of the now-untracked value go silent — the pre-existing
  unknown-return semantics, identical to ADR-0025's repair.
- Removing wrong borrow facts dissolves whole-summary conflict collapses
  that were triggered by them (`Unknown` vs `BorrowFromArg` on multiple
  returns). Where the remaining returns are compatible, param effects
  become decidable again through the (unchanged, sound) member/deref rules,
  and the previously-poisoned summaries become decided. Verified for the
  sqlite `sqlite3VdbeGetOp` chain: its `&p->aOp[addr]` return reads through
  a pointer member (`Op *aOp`) — the returned interior belongs to a
  separate allocation, so both of its returns are now honestly `Unknown`
  and the conflict is gone; the fail-closed holding for the unknown return
  sits at its consumption sites (`unknown-pointer-return-ownership` at
  `vdbeaux.c` ChangeP1/P2/P3).
- Interior array-member returns that the old fallback *failed* to resolve
  (the `IgnoreParenCasts`-stripped `MemberExpr` has array type, so the old
  `isPointerType` guard skipped it, collapsing the summary through joins)
  now resolve correctly: `git_reference_name` returning the flexible array
  member `ref->name`, `git_index_checksum`/`git_indexer_hash` returning
  `checksum[GIT_HASH_MAX_SIZE]` — sound `borrow_from_arg@0` interiors.

## Evidence and regression

- Permanent paired fixtures in `tests/interprocedural/`:
  `compound_origin_conditional_incomplete.c` (incident reproducer; must be
  INCOMPLETE, never PASS), `compound_origin_condptr_incomplete.c`
  (pointer-condition form), `compound_origin_comma_uaf.c` (comma form; must
  remain a FAIL detection), `compound_origin_subscript_incomplete.c`
  (value-read form), `compound_origin_callargs_incomplete.c`
  (call-arguments containment form; pre-fix this was a false FAIL with 3
  findings on legal code), `compound_origin_controls_safe.c` (every
  accepted origin form must stay decided — direct, `&p->f`, array-member
  decay, nested chains, `&p[i]`, `p + n`, `c ? p : p`, `c ? p : NULL`,
  comma-on-last, and the ADR-0025 form `p = r; return r;`; if any
  over-collapses this file gains obligations and stops passing) and
  `compound_origin_controls_detect.c` (destroying the origin of a decided
  borrow must stay a FAIL detection).
- Full gate on the fix head: `scripts/check.sh`, `git diff --check`,
  complete CTest (20/20), CVE replay (all entries within recorded
  classifications), interprocedural suite.
- Five-pilot before/after (post-#62 baseline vs fix, provenance builds;
  changed summaries: hiredis 15, zlib 4, curl 45, libgit2 131, sqlite 107):
  - No function became clear in hiredis, zlib or curl. Seven functions
    became clear in libgit2/sqlite (`git_reference_name`,
    `git_index_checksum`, `git_indexer_hash`, `git_indexer_name`,
    `git_index__checksum` — verified sound interior array-member returns —
    and `sqlite3VdbeJumpHere`, `sqlite3VdbeJumpHereOrPopInst`,
    `sqlite3VdbeExplainPop`): their pre-fix blockers were cascade artifacts
    of conflict collapses triggered by wrong borrow facts; their bodies are
    sound and the underlying unknowns are held at direct consumption sites.
  - All summary movement is Unknown-ward or conflict-resolving: 262
    `borrow_from_arg/owned → unknown` returns, 0 returns newly decided
    except the five verified interior accessors above, 0 decided params
    became unknown except 3 sqlite positions inside 2 newly fail-closed
    conflicts, 29 params became decided through conflict resolution, and
    one origin *correction* (curl `Curl_hash_add` 0→3: the callee-mapping
    branch now runs and `Curl_hash_add2` returns its param `p`).
  - Eight `CAND-B003` findings were removed (hiredis `async.c:154`,
    `hiredis.c:1222`, `sds.c:540`, curl `multi_ev.c:90`, libgit2
    `repository.c:3307`, sqlite `build.c:755`, `printf.c:1371`), each with
    `borrow.origin = verified-summary:<callee>` where the callee's
    `borrow_from_arg` claim was an incident-family misattribution
    (allocator wrappers `hi_realloc`/`hi_malloc` claiming borrows of their
    `size_t`/realloc-moved arguments, `sdscat` claiming a borrow through
    its call arguments while `sdscatlen` may reallocate, member-value
    reads `repo->commondir`, containment-mapped wrappers). The B003
    premises were the defect itself; their removal is defect-artifact
    removal, not lost valid findings. Reviewer sign-off on this
    adjudication is requested in issue #64.
  - zlib: 4 summaries changed (allocation wrappers `gzopen`/`gzopen64`,
    `zError`), findings preserved 2/2.

## Claim impact

The public C&1/v1 claim of v0.2.0 and v0.2.1 remains suspended
(`docs/SAFETY_CLAIMS.md`); the suspension introduced for incident #62 is
extended to this defect. Claim restoration requires a post-incident release
with complete exact-head requalification per
`docs/CAND1-FALSE-PASS-RESPONSE.md`. Tags are not rewritten.
