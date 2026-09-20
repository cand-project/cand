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
