# C&1 Milestone #54 Final Report — Alias/Storage Evidence Bar + Local-Alias Destruction Repair

Conclusion: **evidence bar evaluated and recorded (adoption precision
deferred); the local-alias destruction false FAIL repaired and
qualified.** The Gate A measurement shows alias/storage is not the
dominant adoption blocker outside zlib, so issue #54's precision work
stays deferred per the issue's own bar. The separately evaluated known
false FAIL (a verifier bug producing 50% of all pilot findings) was
repaired with exactly one bounded rule (ADR-0028) and passed full Gate
B qualification. No soundness incident occurred; no false PASS is
possible by construction (fail-closed direction), which the fixtures,
the differential campaign, and the five-pilot before/after all
confirm.

1. Starting point: main `deb692a` (v0.2.2, the qualified C&1/v1
   identity; semantic source `cc8e9bd`). The follow-up PR #68
   (release-evidence doc line) was pending and is untouched by this
   milestone; this branch is based on `origin/main`.
2. Gate A harness (`scripts/pilots/alias_storage_pareto.py`): reuses
   `cross_tu_pareto.py` workdirs and attribution, with a finer family
   split (PTR-OUT, URET separated from OTHER). Evidence document:
   `docs/pilots/ALIAS-STORAGE-PARETO.md` (committed with this PR).
3. Evidence bar, condition (a) — NOT met overall: sole-ALIAS-blocked
   functions 7/18/98/71/145 (hiredis/zlib/curl/libgit2/sqlite), #1
   only in zlib; the callee-summary cascade (STU, the milestone #61
   residual) leads in the four large pilots. Reconciled with the
   committed #42 census (different TU partition, same conclusion).
   The zlib exception is recorded as the leading indicator to re-run.
4. Evidence bar, condition (b) — NOT met: neither CVE replay defect
   path is alias/storage-blocked (cJSON `merge_patch` is
   pointer-output/call dominated; libexpat's mechanism is callback
   reentry, excluded scope).
5. The known false FAIL: reproduced (`q = p; free(q)` → CAND-O006
   while direct `free(p)` → correct Destroy summary); mechanism
   located (syntactic call-effect scan vs alias-resolved flow
   capability); measured frequency 23 of 46 pilot findings (zlib 2/2,
   curl 21/42) — a C3 finding-ground-truth problem in its own right.
6. Interpretation (flagged for the reviewer): issue #54's dominance
   bar is read as gating the adoption-precision track; the issue's
   own weaker evaluation clause (frequency, bounded design,
   soundness risks, corpus impact) governs the false-FAIL repair and
   is fully satisfied in the evidence document §3.
7. The rule (ADR-0028): consuming calls through a local that
   unambiguously holds one parameter's entry value (single-assignment
   declaration-init alias; local never reassigned, address never
   taken, not volatile/atomic/static; parameter never reassigned)
   attribute their effect to that parameter. Consuming effects only
   (`free` or Destroy/TakeOwnership callees); everything else keeps
   the pre-rule behavior. Two surfaces: the `scan` argument resolver
   (new) and the existing summary→capability derivation (unchanged).
   The flow checker's destroy paths are untouched.
8. Fixtures: 17 paired regression fixtures
   (`tests/interprocedural/parameter_alias_*.c`) pin the repair, every
   whitelist boundary, the fail-closed complement, the caller-side
   destroy semantics, the loop strengthening (O006 → double-destroy),
   the annotation parity, and the two documented residual false FAILs.
9. Fuzz: 6 new differential mutation operators (`PARAM_ALIAS_*`)
   cover the parameter-alias surface (destroy/use, safe, conditional,
   reassigned, two-parameter mis-attribution in both directions);
   pre-existing local-owner alias operators unchanged (scope guard).
10. Five-pilot before/after (measured, every finding delta
    adjudicated): curl findings 42 → 23 (19 CAND-O006 false FAILs
    removed; zero new findings anywhere), curl clear 345 → 347
    (`rtsp_easy_dtor`, `smtp_easy_dtor` — blocked solely by the false
    FAIL); all other pilots completely unchanged; obligations
    byte-identical in every pilot (886/911/15622/17013/20921).
11. Residual false FAILs (4, by design, recorded with populations):
    2 curl loop-reassigned shapes (`Curl_freeaddrinfo`,
    `curl_slist_free_all`) and 2 zlib assignment-established shapes
    (`gzclose_r`, `gzclose_w`); the `q = p; p = NULL; free(q)`
    transfer idiom and transitive aliases remain fail-closed
    (adjacent to #25).
12. Qualification: repository checks (`scripts/check.sh`) green;
    CTest 20/20; CVE replay 2/2 within recorded classifications; the
    extended differential fuzz campaign (seed 12345, 10,000 cases,
    same corpus as the v0.2.2 requalification plus the six new
    operators) — results recorded below.
13. Claim boundary: a false-FAIL repair cannot weaken the C&1/v1
    claim on the v0.2.2 identity (no false PASS by construction and
    by measurement); the changed verdict semantics are subject to
    requalification at the next release. SAFETY_CLAIMS needs no
    suspension.

## Campaign results

Extended differential fuzz campaign (the fresh campaign this
milestone's Gate B requires, since verdict semantics change):

- configuration: seed 12345, 10,000 cases, extended mode, profile
  `cand1/v1`, generator `cand1-fuzz/v2-source-driven` — the same
  corpus configuration as the v0.2.2 requalification seed, giving
  direct comparability, plus the six new `PARAM_ALIAS_*` operators;
- case results: 3,334 `CORRECT_PASS`, 3,333 `CORRECT_FAIL`, 3,333
  `CORRECT_INCOMPLETE`; **0 `FALSE_PASS`, 0 `FALSE_POSITIVE`, 0
  `COVERAGE_GAP`, 0 `WRONG_FAILURE_CLASS`, 0 harness errors** (the
  acceptance set is exactly zero for each — a single miss is an
  incident, none occurred);
- mutation operators: **41/41 correct** — the 35 pre-existing
  operators unchanged (including `LOCAL_ALIAS_DESTROY_USE`
  CORRECT_FAIL, `LOCAL_ALIAS_TRANSFER` CORRECT_PASS,
  `CONDITIONAL_ALIAS_DESTROY_USE` CORRECT_FAIL: the local-owner scope
  guard holds), and the six new `PARAM_ALIAS_*` operators correct
  (`DESTROY_USE`/`TWO_PARAM_USE` CORRECT_FAIL, `DESTROY_SAFE`/
  `TWO_PARAM_SAFE` CORRECT_PASS, `CONDITIONAL_DESTROY_USE`/
  `REASSIGNED` CORRECT_INCOMPLETE, the fail-closed classes);
- ground truth: 3,333 of 3,333 runtime-exercisable violations
  individually confirmed by ASan (1,060 double-free, 2,273
  heap-use-after-free; 0 non-runtime or unconfirmed);
- protocol/adversarial: 24 protocol cases (0 authority bypasses, 0
  frontend bypasses), all 34 declared source mechanisms exercised,
  deterministic JSON verified;
- report: `/tmp/opencode/rel/fuzz-a54/seed12345/report.json`
  (scratch, not committed; schema `cand.fuzz-report/v2`).

## Recommendation for the next milestone

Per the measured evidence: the callee-summary cascade (milestone #61
residual same-TU families H12/H5/H11ind, or the cross-TU family) is
the dominant adoption blocker; alternatively the recorded zlib alias
dominance justifies a scoped alias/storage campaign if the large
pilots' callee families shrink first.
