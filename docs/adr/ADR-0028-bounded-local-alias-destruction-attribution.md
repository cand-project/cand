# ADR-0028 — Bounded Local-Alias Destruction Attribution (Milestone #54)

Status: accepted (2026-09-23, milestone #54 Gate B)

## Context

Issue #54 documents a known precision defect (a false FAIL, not a
soundness violation): destruction of a parameter through a local alias
is misattributed as destruction of a *borrowed* parameter.

```c
static void run(int *p) { int *q = p; free(q); }  /* CAND-O006: borrowed parameter cannot be destroyed */
int main(void) { int *x = malloc(sizeof *x); run(x); return 0; }  /* legal program, false FAIL */
```

`SummaryBuilder`'s call-effect scan is syntactic: it recognizes a
consuming call only when the argument expression directly contains the
parameter. `int *q = p` marks the parameter `Borrow`
(`markParameterFlow`); `free(q)` records nothing (the argument is a
local). The flow checker — whose parameter capability is seeded from
that summary — resolves `q`'s storage binding to the parameter object
and reports `CAND-O006 ownership.destroy-borrowed-parameter`. The
direct form `free(p)` produces the correct `ParamEffect::Destroy`
summary and a caller-side PASS: two syntactic forms of the same
semantics diverge.

The milestone #54 Gate A measurement
(`docs/pilots/ALIAS-STORAGE-PARETO.md`) recorded the evaluation the
issue requires before implementation:

- **Frequency:** 23 of 46 total pilot findings (50%) are CAND-O006 of
  this class (zlib 2/2, curl 21/42, hiredis/libgit2/sqlite 0). A FAIL
  verdict of which half is bogus is a finding-ground-truth (C3)
  problem in its own right, independent of the alias/storage
  adoption-precision question, which issue #54 gates behind its
  dominance evidence bar (that bar is recorded as NOT met in the same
  document; this repair is the issue's separately evaluated
  "Known precision defect" track).
- **Corpus impact:** the defect does not fire on the CVE replay corpus
  (both entries report zero findings) and does not touch hiredis.

## Decision

Add one bounded origin-resolution rule to `SummaryBuilder::scan`, in
the ADR-0026 whitelist family. When a call argument is a local
variable that unambiguously holds one parameter's entry value, and the
call's effect on that argument is consuming, resolve the argument to
that parameter for the effect computation — exactly as the direct
`free(p)` form.

The whitelist (every condition must hold; any failure leaves the
pre-rule behavior in place, fail-closed):

1. the argument is, after paren/cast stripping, a plain reference to a
   function-local pointer variable — not a parameter, not a static
   local, neither volatile nor atomic;
2. the local's initializer is, after paren/cast stripping, exactly one
   parameter reference (no derived, field, conditional, call-shaped,
   or multi-parameter initializers; `c ? p : p` included);
3. the local is never the left-hand side of an assignment anywhere in
   the body, and its address is never taken anywhere in the body;
4. the aliased parameter is never assigned in the body.

The resolution applies only to consuming effects: a direct `free`, or
a callee whose summary parameter effect is `Destroy` or
`TakeOwnership`. Unknown callees and borrowing callees are untouched,
so the `unknown-call-with-tracked-pointer` escape path is unchanged.

Two surfaces: the `scan` argument resolver (new), and the existing
capability derivation that seeds the flow checker from the summary
(unchanged). The flow checker's `handleFree`/`destroyBinding` paths
are deliberately untouched: with the summary now `Destroy`, the
CAND-O006 branch is not taken and the destruction proceeds through
the full direct-free path (mark dead, double-destroy checks, borrow
invalidation) — the masked genuine use-after-destroy cases become
findings at the correct locus.

## Soundness analysis

Over-attribution of destruction is fail-closed: a caller that is
wrongly told its argument was destroyed can only receive a false FAIL
(use-after or double-destroy), never a false PASS. The unsound
direction — dropping a real destruction — is what the whitelist
prevents: the rule fires only when the local provably holds the
parameter's entry value at every point (declaration-initialized from
the parameter, never reassigned, address never taken, parameter never
reassigned).

Boundaries that stay fail-closed (each pinned by a regression fixture
in `tests/interprocedural/`, see `parameter_alias_*.c`): conditional
initializers and conditional destruction (ADR-0027 join to Unknown),
reassignment of the local or of the parameter (including the
`q = p; p = NULL; free(q)` transfer idiom — known residual false FAIL
adjacent to #25), transitive aliases, address-taken locals, loop
shapes (which become double-destroy FAILs, strictly stronger than the
previous single O006), and unknown callees. Annotation parity: a
consuming body remains authoritative over a `cand:borrow*` annotation
exactly as for the direct free form (pinned by
`parameter_alias_annotated_borrow_body_authority_safe.c`).

## Consequences

- The measured five-pilot before/after (same document, section 3.5):
  curl findings 42 → 23 (all 19 removed are O006 of this class; no
  new findings anywhere; obligations byte-identical in every pilot),
  curl clear 345 → 347 (the two functions previously blocked solely
  by the false FAIL); every other pilot completely unchanged.
- Four residual false FAILs remain by design (2 curl loop-reassigned
  shapes, 2 zlib assignment-established shapes) and are recorded as
  residual debt with their measured populations.
- The differential fuzz suite gains six parameter-alias mutation
  operators (`PARAM_ALIAS_*`) covering the destroy/use, safe,
  conditional, reassigned, and two-parameter mis-attribution probes;
  the pre-existing local-owner alias operators are unchanged and
  continue to pin the scope boundary.
- Claim boundary: the repair is a false-FAIL (fail-closed direction)
  fix; it cannot produce a false PASS and therefore does not weaken
  the C&1/v1 claim on the v0.2.2 identity. The changed verdict
  semantics are qualified by the full gate (repository checks, CTest,
  CVE replay within recorded classifications, five-pilot
  before/after, extended differential fuzz campaign) and are subject
  to requalification at the next release.
