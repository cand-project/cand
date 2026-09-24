# ADR-0031 — Borrow-idiom Modeling: Address-of-Parameter Borrows, Integer-Delta Cursor Advances, and Verified-Origin Borrow Returns (Issue #73)

Status: accepted (2026-09-24, milestone #73)

## Context

Issue #73: the #36 E2 measurement recorded six fail-closed artifacts on
correct C in the pilots, attributed to three recurring borrow idioms:

| # | Site | Artifact |
|---|---|---|
| 1 | hiredis `read.c:168` (`seekNewline`) | `CAND-B003` "borrow escapes through an undeclared return" |
| 2 | libevent `buffer.c:1541/1542/1544` (`find_eol_char`) | `CAND-B003` (three) |
| 3 | redis `zmalloc.c:541/568` (`ztryrealloc_usable`/`zrealloc_usable`) | `CAND-B003` (two) |
| 4 | libevent `evutil.c:3211` (`setsockopt(..., &on, ...)`) | `unmodelled-pointer-parameter` |
| 5 | hiredis `net.c:248` (`setsockopt(..., &tv, ...)`) | `unmodelled-pointer-parameter` |
| 6 | sqlite amalgam `sqlite3.c:85990` (`memcmp(&r1, &r2, ...)`) | `unmodelled-pointer-parameter` |

A TU-level re-verification corrected the issue's framing before any design
work: instances 1–3 are not `borrow-unknown-parent` (that is a separate
Unsupported kind for missing/dead parents, which none of the six trips);
all five findings are the identical mechanism — a local holding a shared
borrow created by `bindSummaryReturn` from a `BorrowFromArg` summary
(`verified-summary:memchr` / `extend_to_usable`) is returned while the
enclosing function's own summary return effect is `Unknown`, because
`SummaryBuilder` has no local-origin propagation and incident #62's
order-insensitive `assignedLater` defeats the cursor shapes. Instances 4–6
are `&param-scalar` arguments at contracted borrow parameters, where the
address-of-*local* twin already passes silently — an asymmetry with no
dedicated test. The same cursor functions also carry collateral
`pointer-arithmetic-reassignment` / `ambiguous-alias-target` rows from the
any-compound-assignment storage poisoning.

The incidents' discipline constrains any design: repairs must keep wrongly
*decided* summaries honestly *undecided* (ADR-0025, ADR-0026); new
precision must be sound (adversarial fixtures proving real
use-after-frees through the same idioms must still FAIL); and the
fail-closed boundary stays where verification, not plausibility, ends.

## Decision

### 1. Area A — address-of-pointer-free storage at borrow-effect arguments

In `checkAccess`'s untracked-argument path, an `AddrOf` whose pointee
type is pointer-free (`!typeMayContainPointer`) is ownership-neutral and
emits no obligation. The argument is the callee-local copy of a
pointer-free stack slot; a read — or a read-or-write per the reviewed
borrow claim class (e.g. `getsockopt`'s `optval`) — of pointer-free
scalar storage cannot fabricate, duplicate, or clobber a tracked pointer.

Scope notes:

- The rule applies only where nothing inside the `AddrOf` is tracked
  (tracked `&p[0]`/`&s->f`/`&p` forms resolve earlier through
  `findTrackedBinding` and are unchanged). The type filter is
  conservative scoping, not the soundness load-bearer.
- `&pointer` (pointee is a pointer) and `&struct-with-pointer-member`
  keep today's behavior: tracked forms resolve silently; untracked
  forms keep the obligation.

### 2. Area C — pure-integer-delta pointer arithmetic preserves the parent

`PointerRelation` gains `Interior` (no `Interior -> Base` path; joins
only degrade `Interior` to `Unknown` keeping the object id). A pointer
storage advanced by a **pure-integer delta** — `p += e`, `p -= e`,
`p++`, `p--`, and the cross-lvalue `q = w ± e` — keeps its object id
with relation `Interior` and emits no
`pointer-arithmetic-reassignment` obligation. A delta that mentions any
pointer value (`q - p`) poisons as before.

Soundness boundary (stated, not implied): the claim is scoped to
well-defined executions. Adding an integer delta to a pointer into
object B yields a pointer still derived from B, or an out-of-bounds
pointer, which is UB — cand treats it as still borrowing B, which can
only add detections (if B dies, uses still fail). The only well-defined
way arithmetic can rebind to another object is pointer-difference
arithmetic (`p + (q - p) ≡ q`), which requires `q` to point into the
same array — the same object — or is itself UB; hence
pointer-mentioning deltas poison. The two-step form (`d = q - p;
p += d`) preserves the parent under the same argument and is pinned by
a fixture with its documented verdict.

Mandatory companion — **exact-base-required destruction**: destroying,
freeing, consuming, or moving a pointer whose relation is not `Base`
(while it still holds a live object id) emits the new
`destroy-of-non-base` obligation. This applies in all four destruction
paths (`free`, `Destroy` effect, `TakeOwnership` transfer, move).
Without it, `p += 1; free(p)` would become an accepted free of an
interior pointer. `tests/storage/compound_pointer_advance_incomplete.c`
and `tests/storage/unary_pointer_advance_incomplete.c` remain
`INCOMPLETE`.

An owner advancing its own cursor is not an object write;
`checkMutableOwnerAccess` remains owner-gated, so a `B004` conflict is
not manufactured by the advance itself (previously masked by poisoning,
now correctly silent when no other violation exists).

### 3. Area R — verified-origin return resolution (summary-side dataflow)

`SummaryBuilder` computes, per function, an intraprocedural,
flow-insensitive, **monotone-join** origin-set dataflow over pointer
variables: sets over `{param 0..n-1, FRESH}` with join = union and an
unresolvable top; no transfer narrows; loops iterate to a fixpoint. It
is used **only** as a fallback to resolve the function's summary return
effect when today's direct paths leave it `Unknown`: all return sites
must agree on a singleton `{j}` (or `{j, NULL}`) → `BorrowFromArg(j)`;
`{FRESH}` → `Owned`; anything else stays `Unknown` — exactly today's
behavior. Parameter-effect logic and the flow-level B003 backstop are
untouched.

Transfers mirror the incident-#64 whitelist: a `DeclRef` reads the
variable's *current map entry* (reassignments join — the #62 guard done
properly); `w ± e` with the Area C pure-integer-delta filter; `&w->f`,
`&w[i]` (pointer base), and array-decay shapes resolve to the base;
conditionals join their arms (NULL is absorbed by any non-empty set and
never a singleton alone); comma takes the last operand; a call composes
with the callee's `BorrowFromArg(k)` summary over the argument's join
set (or `Owned`/`malloc` → `FRESH`). Pointer-typed value reads
(`w[i]`, `w->f`), `&w`, `*w`, globals, and volatile/atomic accesses are
unresolvable (the incident-#64 class;
`compound_origin_subscript_incomplete.c` must not flip). Locals with
explicit `cand:borrow`/`cand:borrow_mut`/`cand:borrow_shared`
annotations are excluded — annotated borrows still require
`CAND_RETURNS_BORROW_FROM` (SPEC-0005 §6; `tests/p2/
undeclared_borrow_return.c` stays FAIL). A variable whose own storage
address escapes anywhere in the body is unresolvable.

This is **resolution-with-detection, not suppression**: the caller
receives a `BorrowFromArg(j)` summary and `bindSummaryReturn` creates
the borrow against the caller's actual-`j` object, so a use-after-free
through the returned borrow in the caller is still detected
(`borrowed_local_return_uaf_detect.c` fails with `CAND-B001`+`B002`
after the change; a wrong resolved index would pass silently, which is
why the dataflow's monotone-join property is a soundness requirement,
not a style choice — `v = p; if (c) v = q; return v;` must join to
`{0,1}` → `Unknown`).

### 4. Residual debt, honestly recorded

redis `zmalloc.c:541/568` **remain** `CAND-B003`: the wrapper's `ptr`
is reassigned from `ztryrealloc_usable_internal(...)` whose return
effect is `Unknown` (`je_realloc_with_usize` is uncontracted), so the
origin set is unresolvable end-to-end. Deciding it requires
realloc-family lifetime-replacement modeling, which is out of scope
and not claimed. The declaration-form advance `T *q = p + 1;` still
emits `ambiguous-alias-target` (fail-closed); only the assignment and
compound forms are modeled.

The S6 re-measurement surfaced one **new** residual of the same
finding class, recorded with the others in issue #76: redis
`listpack.c:916` (`lpFindCbInternal`) returns a cursor whose origin
joins {param 0, param 1} — genuinely not a singleton — so the summary
stays `Unknown` and the B003 backstop fires where the old
poisoned-cursor path emitted an obligation (TU incomplete → fail;
fail-closed direction on correct code). Deciding it needs a summary
vocabulary for disjunctive return origins, which the single-parent
borrow model cannot represent. The same measurement showed
`combineReturn`'s pre-existing cross-site agreement check marking
newly-mixed resolved/unresolved return sites (`lpSeek`) as
`contract-body-conflict` instead of `Unknown` — fail-closed either
way, recorded for policy review.

## Consequences

- The paired regression corpus in `tests/interprocedural/` (18
  fixtures, ADR-0027 convention) pins every boundary: intended unlocks
  (`addr_param_scalar_borrow_safe`, `cursor_integer_advance_use_safe`,
  `borrowed_local_return_safe`, the faithful `seekNewline` and
  `find_eol_char` shapes, the same-TU local-copy chain), fail-closed
  controls (two-param, loop-carried, and two-param-null joins;
  variable-delta and two-step rebinds; the roundtrip and
  destroy/move-of-interior pins), and detection controls
  (`cursor_integer_advance_uaf_detect` → `CAND-B002`;
  `borrowed_local_return_uaf_detect` → `CAND-B001`+`B002`).
- The #62/#64/#61 corpora are byte-identical: the subscript, comma,
  callargs, conditional/condptr, controls, reassigned-*, all six
  `conditional_join_*`, `undeclared_borrow_return`, and
  `direct_global_escape` fixtures re-ran verdict-identical after each
  stage. Two implementation regressions were caught by these gates
  during development (a DeclRef transfer reading the seed instead of
  the map entry — the exact #62 false-PASS shape — and a null RHS in
  compound-advance evaluation).
- Full qualification battery green at the final tip:
  `scripts/check.sh`, CTest 20/20 (including fast fuzz and
  policy-attacks), CVE replay 2/2 within recorded classifications
  (both entries remain BOUNDED-INCOMPLETE).
- Real-world re-measurement (all 8 pilots × {b0, e2, e2plus}, old vs
  new binary, identical bundles — `docs/CAND1-73-FINAL-REPORT.md`):
  4,538 obligations removed vs 462 added (all fail-closed, all inside
  already-blocked functions; net down in every pilot×config cell);
  the four correct-C `CAND-B003`s removed (TU fail→incomplete,
  `seekNewline`/`find_eol_char` CLEAR); both baseline guard violations
  restored (`evutil.c:3211`, `sqlite3.c:85990` — 24 amalgam rows);
  201 CLEARs gained, 0 lost; 0 false PASS (the one TU→pass, zlib
  `adler32.c`, is a pure-read cursor loop with no destruction); one
  new fail-closed finding (redis `listpack.c:916`, §4/issue #76) and
  one correct `destroy-of-non-base` detection on a real interior free
  (sqlite `sqlite3MemFree`); zmalloc's two findings and the
  declaration-form residual remain fail-closed.
- SPEC-0005 §4/§6/§7 and SPEC-0010 §4 rule 10 are amended to record
  the three decidable forms and their boundaries.
- `destroy-of-non-base` is added to the pilot census taxonomy
  (`KIND_TO_SLOT`, unsupported alias/storage slot).
