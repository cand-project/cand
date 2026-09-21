# C&1 adoption blocker Pareto: Hiredis

This is an evidence-ranked roadmap, not a promise of coverage. Counts are
Hiredis H3 diagnostic observations and are nonexclusive where stated. No item
below changes C&1/v1 semantics in this phase.

| Candidate | Current affected obligations | Affected functions | Existing metadata solve? | Semantic change? | Schema change? | Soundness risk | Expected adoption benefit | Recommended phase |
|---|---:|---:|---|---|---|---|---|---|
| Pointer parameter effects | 685 `unmodelled-pointer-parameter` | 100 | Partly: current summaries represent borrow/take/destroy; 608 observations lack reliable parameter identity | Not for the already represented effects; broader transport may be new scope | Possibly | high if storage/alias identity is guessed | high, but only 77 observations were directly attributable to a named parameter | minimize and qualify a parameter-observation improvement |
| Same-project cross-TU summaries | 29 directly identified candidates: 12 return effects + 17 calls to `__redisSetError` | 2+ caller/definition boundaries | Reviewed contracts solve selected external symbols; automatic cross-TU facts are not v1 | Yes, under current SPEC-0010 boundary | likely | high | bounded, measurable unlock | new qualified profile/version candidate |
| Pointer-output effects | 96 `unknown-call-with-pointer-output` | 5 files | No; `produces_out_owner` is documented but not loaded by current implementation | Yes | yes | very high | potentially high | new profile candidate with out-owner regressions |
| Unknown pointer returns | 199 including qualified symbol variants; 80 base unknown-return rows | 5 files | Contracts solve direct external returns; declaration annotations did not resolve the external-only fixture | Existing direct external contracts fit v1; automatic inference does not | no for reviewed contracts | medium/high | useful at API boundaries, but Hiredis TU totals did not improve | contract ergonomics first |
| Alias/storage precision | 123 ambiguous alias targets plus 824 pointer-storage-related rows overall | multiple | No | likely | possibly | high | high | minimized alias/storage fixtures |
| Realloc | 0 direct H3 diagnostics tagged `realloc` | — | No safe current rule | Yes | yes | very high | unknown from this corpus | separate soundness study |
| Aggregate transport | no direct H3 kind tagged aggregate | — | No general rule | likely | yes | high | unknown | retain fail-closed |
| Callback retention | async boundary remains excluded; no direct callback kind in H3 rows | async functions | No | Yes | yes | very high | important for async, not synchronous-core adoption | separate profile candidate |
| Contract/body reconciliation | 29 H3 conflicts; 21 overlap H2, 8 are additional contract/body locations | 24 functions | Matching fixture is PASS; conflicting fixture is fail-closed INCOMPLETE | A compatible reconciliation rule may fit v1; override would not | likely | very high | medium/high because current contracts add obligations | contract/body fixture and schema design |
| Function-level reporting | 181 functions; 0 VIOLATION/TOOL-UNKNOWN in this run | all scope | Reporting tool now maps all rows | No | no | low | high observability, no coverage gain | available now; keep non-authoritative |

## Interpretation

The largest measured set is pointer/storage handling, but most of those
observations are not attributable to a single source parameter. The clearest
bounded unlock is reviewed external contracts: fixtures show owned returns and
consumed parameters can move INCOMPLETE to PASS, while pointer-to-pointer
outputs, realloc, callback retention, and conflicting body facts remain
INCOMPLETE.

The 29 same-project cross-TU candidates are evidence for a follow-up, not
permission to add cross-TU semantics to C&1/v1. Redis is deferred until one of
these bounded improvements demonstrates a material, sound reduction in a
realistic corpus.

## Addendum: post-parameter-identity-repair re-baseline (ADR-0024)

The top row above ("Pointer parameter effects", 685
`unmodelled-pointer-parameter` observations over 100 functions) was measured
before the parameter-identity completion. Re-measured on the same pinned
Hiredis tree (`redis/hiredis@33a12fb`) after ADR-0024, with a corrected
measurement method (the earlier baseline-with-contracts comparison had
mistakenly compared the patched binary against itself; both binaries are now
built and pinned separately):

| Config (same tree, same invocation) | Obligations | `unmodelled-pointer-parameter` | Reported escapes | Findings | CLEAR / BLOCKED functions |
|---|---:|---:|---:|---:|---|
| baseline v0.2.0, no contracts | 1,247 | 674 | 33 | 0 | 32 / 147 |
| baseline v0.2.0 + libc-borrow bundle | 1,237 | 708 | 26 | 0 | 36 / 143 |
| ADR-0024 seeded, no contracts | 987 | 0 | 391 | 3 | 13 / 166 |
| ADR-0024 seeded + libc-borrow bundle | 887 | 2 | 333 | 4 | 17 / 162 |

Method note: unlike the per-TU strict generated-policy H3 methodology of the
original table above, these are whole-corpus single runs in the default
semantic profile (179 defined functions across the seven representative
translation units); they are internally comparable but not row-for-row
comparable with the original H3 counts. The final seeded+bundle row reflects
the complete change set, including two review corrections made after the
first measurement: the `strncpy` borrowed-return declaration (removing one
spurious `unknown-pointer-return-ownership` at `net.c:688`) and the
variadic-argument escape fix (adding one genuine
`unknown-call-with-tracked-pointer:snprintf` escape at `net.c:108`, where a
tracked parameter is read at a variadic position — see ADR-0024's companion
soundness fix). Both affected functions were already BLOCKED, so CLEAR/BLOCKED
counts are unchanged.

Reading of the re-baseline:

- The 674-observation `unmodelled-pointer-parameter` blocker class is
  eliminated outright (0 without contracts; 2 residual with the libc bundle,
  both by-value struct-parameter address-taken sites in `net.c`, not pointer
  parameter binding).
- CLEAR functions drop 32 → 13 not because valid programs were lost, but
  because 19 functions whose pointer parameters silently escaped to opaque
  callees previously received untrustworthy PASS verdicts; those escapes are
  now reported (`unknown-call-with-tracked-pointer` obligations rise 33 → 391),
  which is the ADR-0010 soundness restoration, not an adoption regression.
- The reviewed libc borrow bundle remains a net win on both binaries
  (+4 CLEAR on baseline, +4 CLEAR on the seeded build; 10 obligations removed
  on baseline, 100 on the seeded build) and never regresses a CLEAR function.
- Three to four genuine `CAND-B003` return-ownership boundary findings surface
  on the seeded build (undeclared borrow-returns), which were previously
  invisible for the same untracked-parameter reason.

Consequence for this roadmap: the "pointer parameter effects" row is resolved
by ADR-0024 and should be retired from the blocker Pareto. On this corpus the
measured residual obligation classes are now, in order: reported parameter/
allocation escapes (`unknown-call-with-tracked-pointer*`, 333 observations —
55 of them to named same-project callees addressable by cross-TU summaries,
20 to named libc/builtin callees addressable by further reviewed contracts,
and the remainder to generic or indirect callees), alias/storage precision
(`ambiguous-alias-target`, 176), unknown pointer returns (162), and
pointer-output effects (100).
