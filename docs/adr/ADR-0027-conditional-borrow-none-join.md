# ADR-0027 — Conditional Borrow/None Effect Join (Milestone #61)

Status: accepted (2026-09-23, milestone #61 Gate B)

## Context

`SummaryBuilder` collapsed every call effect computed under a conditional
context to `Unknown`:

```cpp
if (conditional) effect = ParamEffect::Unknown;
```

A parameter passed to a *borrow-effect* or *no-effect* callee only under a
condition (`if (p) helper(p);`) therefore made the whole summary
undecided, and every caller of the function was held by
`unknown-call-with-tracked-pointer` obligations.

This is inconsistent with the builder's own treatment of conditional
*direct* borrows: a member access, dereference, or subscript under a
condition already sets the parameter effect to `Borrow` unconditionally
(order-insensitive). Milestone #61's Gate A census
(`docs/pilots/SAME-TU-SUMMARY-PRECISION-PARETO.md`) measured the
conditional-call family (H1) as the dominant addressable root cause of
same-TU summary imprecision on the post-incident main baseline
(`928d9bc`): 54 / 50 / 357 / 669 / 424 undecided functions across the five
pilots, and the only candidate rule with a non-trivial population after
C2/C4/C5 were measured already-sound and C3 near-zero.

## Decision

When a resolved call under a conditional context would give a parameter
the effect `borrow` or `no_ownership_effect`, keep that effect instead of
collapsing to `Unknown`. Every other conditional effect — `take_ownership`,
`destroy`, and unresolved-callee `unknown` — still fails closed exactly as
before.

Soundness: a parameter that is at most borrowed (or untouched) on every
path is at most borrowed overall, which is what the caller-side model
consumes. The join order remains conflict-preserving: a kept conditional
`borrow` that later meets an unconditional or conditional
`destroy`/`take_ownership` conflicts to `Unknown` through the existing
lattice (`borrow ⊔ destroy = unknown`, measured by the branch-conflict
regression), and a kept `none` upgrades to any stronger effect it joins.
The rule touches param effects only; it does not interact with the
return-origin guards of ADR-0025/#62 and ADR-0026/#64, and indirect calls
(unresolved callees) are unaffected because their effect is `unknown`,
which is not kept.

This is a join refinement inside the existing `ParamEffect` lattice — no
new effect kinds, no annotations, no contract changes, no new trusted
input.

## Measured effect (five pilots, post-#64 baseline, counterfactual build
verified verdict-identical to this change)

| pilot | summaries decided | clear | findings | obligations |
|---|---|---|---|---|
| hiredis | 44 | 20 → 20 | 1 → 1 | 904 → 886 |
| zlib | 31 | 48 → 51 | 2 → 2 | 924 → 911 |
| curl | 65 | 336 → 345 | 42 → 42 | 15668 → 15622 |
| libgit2 | 212 | 611 → 650 | 1 → 1 | 17140 → 17013 |
| sqlite | 79 | 345 → 356 | 0 → 0 | 21052 → 20921 |

Findings are preserved exactly in every pilot; zero functions lost clear.
Obligation-set diff: 361 `unknown-call-with-tracked-pointer` removed (the
intended unlocks at calls to newly-decided callees), 26 finer-grained
fail-closed obligations surfaced on newly-modeled call paths
(`ambiguous-alias-target`, `unmodelled-pointer-parameter`) — net −335.
Newly-clear functions are guard-then-init/write patterns
(`git_*_init_options`, `git_oid_*`, `deflateBound`/`deflateReset`/`gzseek`,
`curl_strequal`, `sqlite3Atoi`, `sqlite3_vsnprintf`).

## Regression

Permanent paired fixtures in `tests/interprocedural/`:
`conditional_join_borrow_safe.c` and `conditional_join_none_safe.c`
(intended unlocks, must PASS), `conditional_join_destroy_incomplete.c`
(conditional destroy stays fail-closed), `conditional_join_branch_conflict_incomplete.c`
(branch-divided borrow/destroy stays fail-closed),
`conditional_join_uncond_destroy_safe.c` (conditional borrow joined with
unconditional destroy yields a true `destroy` summary), and
`conditional_join_uaf_detect.c` (destroy-then-call through a decided
borrow param is a FAIL detection — the false-PASS probe; with the callee
decided the violation is *detected*, not merely held).

Full gate on the fix head: `scripts/check.sh`, `git diff --check`,
complete CTest (20/20, including the C&1-E gate, sanitizer differential
and fuzz), CVE replay (all entries within recorded classifications),
interprocedural suite, and the incident regression corpora of #46, #53,
#62 and #64 (byte-identical verdicts except the intended unlocks).
