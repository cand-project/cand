# Same-TU Summary-Precision Pareto (Milestone #61, Gate A)

Measurement frame: E1 pinned pilots (hiredis `33a12fb`, zlib, curl, libgit2,
sqlite; amalgamation and TU-exclusion rules as in
`CROSS-TU-ADOPTION-PARETO.md`), merged contract bundle
`merged-contracts.yaml` (digest `83605e67`).

Baseline: post-incident main `928d9bc` (ADR-0025 and ADR-0026 repairs both
applied). All numbers below are from provenance measurement builds
(`CAND_DUMP_SUMMARIES` reason dumps; never committed) that are
verdict-identical to the repository build on the full micro-fixture corpus.

## Method

`scripts/pilots/same_tu_precision.py` reuses a `cross_tu_pareto.py` workdir
verbatim (cached clang AST dumps, raw report, per-TU summary dump with
reasons), rebuilds the project facts from the same ASTs, attributes every
obligation row to a blocker family, and classifies each undecided
FunctionSummary position by the reason the verifier's own `SummaryBuilder`
recorded for its Unknown. Categories:

| cat | description |
|---|---|
| H1 | conditional borrow / no-effect call collapsed to Unknown |
| H2 | conditional consume/destroy correctly held Unknown (fail-closed) |
| H3 | returned local initialized from a parameter |
| H4/H9/H10 | wrapper (via local) around owned / decided / undecided callee |
| H5 | returned local with no modeled provenance |
| H6 | struct/member/interior pointer returned via local |
| H11stu/xtu/ind/ext | call to undecided same-TU / cross-TU / indirect / external callee |
| H12 | pointer-to-pointer parameter |
| H13 | realloc / whole-summary conflict collapse |
| H15conflict / H15other | conflicting joined effects / other unsupported shape |
| H16 | reassigned borrow-origin parameter (post-#62 sound floor; not a precision candidate) |

## Baseline census (post-#62, post-#64)

| pilot | fns | clear | blocked | sole-STU | undecided | dominant addressable root |
|---|---|---|---|---|---|---|
| hiredis | 181 | 20 | 161 | 21 | 155 | H1: 121 occ / 54 fns / 12 sole-STU callers |
| zlib | 159 | 48 | 111 | 10 | 86 | H1: 126 / 50 / 6 |
| curl | 2151 | 336 | 1815 | 143 | 1673 | H1: 948 / 357 / 43 |
| libgit2 | 3298 | 611 | 2687 | 342 | 2532 | H1: 1249 / 669 / 151 |
| sqlite | 2625 | 345 | 2280 | 285 | 1985 | H1: 1006 / 424 / 84 |

H11stu (call to an undecided same-TU callee) dominates occurrences
everywhere — it is the *cascade*, not a root cause; every category above it
in an undecided summary feeds it. Post-#64, H15conflict has largely
dissolved (26 conflict cascades resolved by removing wrong borrow facts),
which *raised* clear counts relative to the pre-#64 census and shrank
H13. H16 is the deliberate post-#62 sound floor (9/0/7/5/33 functions) and
is out of scope: making those decidable requires flow-sensitive origin
tracking, not a bounded join.

## Candidate rules and measured status

The candidate set from the issue (#61 item 4), with micro-fixture audits
(A4–A8) and population counts on the post-#64 dumps:

| rule | family | status |
|---|---|---|
| C1 conditional borrow/None join | H1 | **debt confirmed**; population 79 / 50 / 363 / 808 / 425 undecided functions; counterfactual measured below |
| C2 same-origin multiple returns | H3/H9 | already sound (A4/A6 micro-fixtures) — excluded |
| C3 local return provenance | H3/H9/H6 via local | **near-zero population post-#64**: 2 / 0 / 0 / 2 / 5 functions (declaration-init locals with unambiguous param/decided-callee/member inits, single assignment); the earlier 104 param-alias occurrences in libgit2 collapse to one header-defined function, and post-#64 the decided-callee subpopulation is empty — excluded |
| C4 decided-wrapper composition | H9 | already sound for direct returns (A6); via-local is C3 — excluded |
| C5 member returns | H6 | direct member/interior returns already sound (A5); value reads are correctly fail-closed post-#64 (ADR-0026) — excluded |

## C1 counterfactual (measured on all five pilots)

Rule: when a resolved call under a conditional context would give a
parameter the effect `borrow` or `no_ownership_effect`, keep that effect
instead of collapsing to `Unknown`. Conditional `take_ownership`,
`destroy`, and unresolved-callee effects still collapse (precise
fail-closed complement, H2). Rationale: a parameter that is at most
borrowed (or untouched) on every path is at most borrowed overall — the
same treatment the builder already applies to conditional direct
member/deref/subscript borrows, which are kept unconditionally today; C1
unifies call-mediated borrows with that.

Measured BLOCKED → CLEAR against the post-#64 baseline:

| pilot | summaries decided | clear | lost-clear | findings | obligations |
|---|---|---|---|---|---|
| hiredis | 44 | 20 → 20 (+0) | 0 | 1 → 1 | 904 → 886 |
| zlib | 31 | 48 → 51 (+3) | 0 | 2 → 2 | 924 → 911 |
| curl | 65 | 336 → 345 (+9) | 0 | 42 → 42 | 15668 → 15622 |
| libgit2 | 212 | 611 → 650 (+39) | 0 | 1 → 1 | 17140 → 17013 |
| sqlite | 79 | 345 → 356 (+11) | 0 | 0 → 0 | 21052 → 20921 |

Findings are preserved exactly in every pilot. Newly-clear functions are
dominated by guard-then-init/write patterns (libgit2's
`git_*_init_options` family, `git_oid_*` parsers; zlib `deflateBound`,
`deflateReset`, `gzseek`; curl `curl_strequal`/`curl_strnequal`, the
`Curl_bufq_*` family; sqlite `sqlite3Atoi`, `sqlite3_vsnprintf`,
`sqlite3VdbeRecordCompare`). hiredis decides 44 summaries and removes 18
obligations but no caller is solely blocked on them (+0 clear).

Paired counterfactual fixtures (scratch, not committed): conditional
borrow → unlock; conditional no-effect → unlock; conditional destroy →
stays INCOMPLETE; branch-divided borrow/destroy → stays INCOMPLETE
(conflict join); destroy-then-call through a decided borrow param →
**upgrades** from obligation to FAIL detection; unconditional-destroy
wrapper → PASS (correct: the summary `destroy` is true). Full
micro-fixture corpus and all incident regressions (#46, #53, #62, #64)
byte-identical except the intended unlocks.

## Comparison against the alternative milestones

| dimension | C1 (this) | #54 alias/storage | #41 pointer-output contracts | #39 annotation propagation | #25 last-use precision |
|---|---|---|---|---|---|
| measured sole-blocked functions | 62 cleared across 4 pilots; 296 static sole-STU attribution across 5 | 6 sole-blockers (hiredis, #54 issue body) | 96 observations (hiredis H3) | 17 reviewed sites (hiredis H2) | P2.1 family, flow-level |
| cross-project reuse | all 5 pilots (79/50/363/808/425) | alias/storage family present in pilots | present | present | present |
| new trusted input | none | none | **requires contract effects (produces_out_owner)** | **requires annotations** | none |
| excluded scopes | none | none | **pointer-output — excluded by #61 constraints** | annotation propagation — excluded (new trusted input) | different family (borrow scope end, not summary precision) |
| soundness risk | low: join refinement in the existing ParamEffect lattice; fail-closed complement is precise; no interaction with the #62/#64 return-origin guards (param-side only) | medium: alias resolution | high: pointer-output ownership | medium | medium: flow-sensitive |
| complexity | one branch in the conditional-effect handler | moderate | large | moderate | moderate |

## Gate A acceptance evaluation

1. **Occurs in ≥3 pilots**: yes — undecided-function population in all
   five; measured clear gain in four. ✓
2. **Existing v1 concepts only**: yes — a join refinement of the existing
   `ParamEffect` lattice (`borrow ⊔ none = borrow` in the at-most
   ordering), unifying call-mediated borrows with the existing
   conditional member/deref/subscript treatment. ✓
3. **No new trusted input**: no annotations, contracts, or configuration. ✓
4. **No excluded scopes**: no pointer-output, callbacks (unresolved
   indirect calls stay Unknown), realloc, arbitrary aliasing, cross-TU
   (the join applies to whatever effect the existing resolution produced),
   or concurrency. ✓
5. **Precise fail-closed complement**: conditional
   `take_ownership`/`destroy`/unresolved effects still collapse to
   `Unknown`; conflicting branch effects still conflict (measured). ✓
6. **Meaningful measured gain**: 62 cleared functions (0.5–2.8% of blocked
   where it fires), 335 obligations removed, and 431 additional decided
   summaries that shrink the H11stu cascade for future rules — the largest
   measured same-TU gain available within the milestone's constraints. ✓

**Gate A verdict: PASS for C1 (conditional borrow/None join). C3 fails the
gain bar (near-zero population post-#64). Proceed to Gate B with exactly
one rule: C1.**
