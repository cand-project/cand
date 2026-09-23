# C&1 Milestone #61 Final Report — Bounded Same-TU FunctionSummary Precision

Conclusion: **bounded precision qualified.** Exactly one rule (the
conditional borrow/none effect join, ADR-0027) passed Gate A acceptance on
measured evidence and completed Gate B qualification. No other candidate
qualified. Two soundness incidents (#62, #64) were discovered by this
milestone's measurement work and repaired before the rule was qualified;
both are documented and closed.

1. Starting point: main `89f65d6` (merged PR #60), re-baselined twice by
   incident merges — #62 via PR #63 (`4627363`) and #64 via PR #65
   (`928d9bc`); all measurements below use the post-#64 baseline unless
   stated.
2. Frame: E1 pinned pilots (hiredis `33a12fb`, zlib, curl, libgit2,
   sqlite) with the merged contract bundle (`83605e67`), identical
   TU/file/include/define configuration to `CROSS-TU-ADOPTION-PARETO.md`.
3. Measurement instrumentation: provenance builds add a
   `CAND_DUMP_SUMMARIES` reason dump recording why each FunctionSummary
   field became Unknown; never committed, verdict-identical to the
   repository build on the full micro-fixture corpus (verified for every
   build used here).
4. Census harness (`scripts/pilots/same_tu_precision.py`): reuses
   `cross_tu_pareto.py` workdirs verbatim (cached ASTs, reports, summary
   dumps), attributes obligations to blocker families, and classifies
   every undecided summary position into the H1–H16 taxonomy by the
   builder's own recorded reasons.
5. Taxonomy extension: H16 marks the post-#62 reassigned-origin sound
   floor so the census does not misattribute it to "other unsupported
   shape"; H16 is explicitly not a precision candidate (9/0/7/5/33
   functions across the pilots).
6. Baseline census (post-#64): clear 20/48/336/611/345, blocked
   161/111/1815/2687/2280, sole-STU 21/10/143/342/285 functions.
7. H11stu (call to an undecided same-TU callee) dominates occurrences
   (516/274/11904/11230/14645) but is the cascade, not a root cause.
8. The dominant addressable root cause everywhere is H1 — conditional
   borrow/no-effect calls collapsed to Unknown: 121/126/948/1249/1006
   occurrences over 54/50/357/669/424 undecided functions.
9. H15conflict largely dissolved after the #64 repair (26 conflict
   cascades resolved by removing wrong borrow facts), raising clear counts
   relative to the pre-#64 census.
10. Candidate C2 (same-origin multiple returns): measured already sound
    (micro-fixtures A4/A6) — excluded from implementation.
11. Candidate C4 (decided-wrapper composition): measured already sound for
    direct returns (A6); the via-local form belongs to C3 — excluded.
12. Candidate C5 (member/interior returns): direct forms already sound
    (A5); pointer-member value reads are correctly fail-closed post-#64
    (ADR-0026) — excluded.
13. Candidate C3 (local return provenance, sound scope = declaration-init
    single-assignment locals): population 2/0/0/2/5 functions post-#64 —
    near-zero (the 104 libgit2 param-alias occurrences collapse to one
    header-defined function; the decided-callee subpopulation is empty
    post-#64) — **fails the meaningful-gain bar**.
14. Candidate C1 (conditional borrow/none join): population 79/50/363/808/
    425 undecided functions — the only candidate with a non-trivial
    population; per the directive, this was established by measurement,
    not assumption.
15. C1 soundness rationale: a parameter that is at most borrowed (or
    untouched) on every path is at most borrowed overall; the builder
    already keeps conditional direct member/deref/subscript borrows, so
    C1 unifies call-mediated borrows with the existing treatment.
16. C1 fail-closed complement: conditional `take_ownership`, `destroy`,
    and unresolved-callee effects still collapse to Unknown (H2);
    branch-divided borrow/destroy still conflicts to Unknown (measured).
17. C1 does not interact with the #62/#64 return-origin repairs
    (param-side rule only) and leaves indirect calls unaffected
    (unresolved effects are not kept).
18. C1 counterfactual measured on all five pilots against the post-#64
    baseline: summaries decided 44/31/65/212/79; clear 20→20, 48→51,
    336→345, 611→650, 345→356 (+62 total).
19. Findings preserved exactly in every pilot (1/2/42/1/0 before and
    after); zero functions lost clear in any pilot.
20. Obligations: 904→886, 924→911, 15668→15622, 17140→17013, 21052→20921;
    obligation-set diff: 361 `unknown-call-with-tracked-pointer` removed,
    26 finer-grained fail-closed obligations surfaced on newly-modeled
    call paths (`ambiguous-alias-target`, `unmodelled-pointer-parameter`)
    — net −335, no findings impact.
21. Newly-clear functions are guard-then-init/write patterns: libgit2's
    `git_*_init_options` family and `git_oid_*` parsers, zlib
    `deflateBound`/`deflateReset`/`gzseek`, curl `curl_strequal`/
    `curl_strnequal`/`Curl_bufq_*`, sqlite `sqlite3Atoi`/
    `sqlite3_vsnprintf`/`sqlite3VdbeRecordCompare`.
22. hiredis decides 44 summaries and removes 18 obligations but clears no
    caller (+0): its H1 callers carry additional blockers — recorded as
    the rule's measured boundary, not a soundness concern.
23. False-PASS probe: destroy-then-call through a conditionally-borrowing
    callee upgrades from an obligation to a FAIL detection (the decided
    borrow param makes the violation *detected*); confirmed temporal
    violation + PASS = 0 on all paired fixtures.
24. Comparison vs #54 (alias/storage: 6 sole-blockers measured in
    hiredis), #41 (pointer-output contracts: 96 observations but
    requires pointer-output semantics and new contract effects — excluded
    by #61's constraints), #39 (annotation propagation: 17 reviewed
    sites, requires new trusted input — excluded), #25 (last-use
    precision: different family): C1 has the largest measured gain
    available within the constraints, with full cross-project reuse and
    the lowest soundness risk (a lattice join with a precise fail-closed
    complement).
25. Gate A acceptance: occurs in ≥3 pilots (population in all five,
    measured gain in four) ✓; existing v1 concepts only ✓; no new trusted
    input ✓; no excluded scopes ✓; precise fail-closed complement ✓;
    meaningful measured gain (62 cleared, 335 obligations removed, 431
    decided summaries shrinking the H11stu cascade) ✓ — **Gate A PASS for
    C1**.
26. Gate B implemented exactly one rule: the conditional borrow/none join
    in the `SummaryBuilder` call-effect handler (ADR-0027); no other
    semantic change.
27. Permanent paired regressions committed:
    `conditional_join_borrow_safe.c`, `conditional_join_none_safe.c`
    (unlocks must PASS), `conditional_join_destroy_incomplete.c`,
    `conditional_join_branch_conflict_incomplete.c` (fail-closed
    controls), `conditional_join_uncond_destroy_safe.c` (true destroy
    summary), `conditional_join_uaf_detect.c` (false-PASS probe, must
    FAIL).
28. Full gate on the Gate B head: `scripts/check.sh` ✓, `git diff
    --check` ✓, complete CTest 20/20 (C&1-E gate, sanitizer differential,
    fuzz, toolchain) ✓, CVE replay within recorded classifications ✓,
    interprocedural suite ✓.
29. Incident regression corpora of #46, #53, #62 and #64: byte-identical
    verdicts on the full micro-fixture corpus except the intended C1
    unlocks.
30. The repository Gate B build is verdict-identical to the C1
    counterfactual measurement build on the entire fixture corpus, so the
    pilot numbers above are the qualification numbers.
31. v0.2.1 was not modified or retagged; the v0.2.0/v0.2.1 claim
    suspension from incidents #62/#64 stands pending the post-incident
    release; this rule is a precision improvement inside the suspended
    claim's semantics, not a claim restoration.
32. Redis remains NO-GO during this milestone (no work started).
33. Incidental findings: this milestone's measurement work discovered
    incidents #62 (borrow-origin misattribution through parameter
    reassignment) and #64 (compound-origin misattribution); both were
    repaired under the incident process with full qualification (issues
    #62/#64, PRs #63/#65, ADR-0025/ADR-0026) before Gate B proceeded.
34. The Gate A census, candidate measurements, counterfactual results and
    the comparison table are recorded in
    `docs/pilots/SAME-TU-SUMMARY-PRECISION-PARETO.md`; the census harness
    is committed as `scripts/pilots/same_tu_precision.py`.
35. Residual same-TU precision debt after C1 (measured): H11ind
    (indirect/callback calls), H11xtu (cross-TU), H12 (pointer-to-pointer
    parameters), H5 (unprovenanced local returns), and the H16 sound
    floor — each requires capabilities outside this milestone's bounded
    scope (callback resolution, cross-TU propagation, pointer-output
    semantics, flow-sensitive tracking).
36. Recommendation: adopt C1 (this PR); the next-highest-value milestones
    by measured evidence remain #54 (alias/storage precision) and #41
    (pointer-output contracts, requires scope expansion), with #61's
    residual H12/H5 families folded into their consideration.
