# C&1/v1 v0.2.1 Final Report

Status: final. Date: 2026-09-22.

> **Erratum (2026-09-22, milestone #58):** the Hiredis blocker-attribution
> figures quoted in sections 9 and 14 of this report (179 functions /
> 17 CLEAR; "23 functions solely blocked on external APIs"; "101
> containing") were produced by the coarser attribution available at
> release time and were corrected the same day by the callee-resolved
> re-measurement in `docs/pilots/EXTERNAL-API-BOUNDARY-PARETO.md`:
> the function universe is **181** (18 CLEAR / 163 BLOCKED), the sole-EXT
> count is **7** (38 functions containing external-boundary obligations).
> The release evidence, verdicts, and all other figures are unaffected.

**Release state:** `v0.2.1` tagged on merge commit
`a362306a741c2af694197fa2b63468f1bd4aa9ea` (PR #56), published at
https://github.com/cand-project/cand/releases/tag/v0.2.1. `main` at `3fc8563`
(PR #57 follow-up). The `v0.2.0` tag is untouched at `05867b9`.

## Report

1. **Incident #53 formalized and closed.** The v0.2.0 variadic false PASS
   (tracked pointers at argument positions beyond a callee's modelled
   parameter list could receive authoritative PASS with zero obligations)
   was filed as a BLOCKER with first-hand reproduction on the immutable tag,
   repaired in the PR #51 lineage, and closed after release with the full
   evidence chain. It is permanently encoded as fuzz generator mechanisms
   (`VARIADIC_ESCAPE_LIVE`/`DEAD`) and a no-PASS regression fixture
   (`tests/interprocedural/variadic_argument_escape.c`).

2. **Governance repaired without rewriting history.** The `v0.2.0` tag was
   not moved or retagged; its C&1/v1 claim is marked SUSPENDED/SUPERSEDED in
   README, SAFETY_CLAIMS, and the v0.2.0 evidence doc (which retains an
   explicit suspension header and remains publicly available). Same
   treatment preserved for v0.1.0.

3. **Exact-head requalification of `1e1f582`: all gates green.** Clean
   checkout, `git diff --check` clean, CTest 19/19, `scripts/check.sh`
   green, toolchain attack suite PASS, independent adversarial corpus
   100/100, CVE replay 2/2, protected CI green.

4. **Reproducibility proven.** Two independent clean builds of `1e1f582`
   are bit-identical (verifier SHA256 `6caa27b6…`). The two additional
   campaign binaries (`0d002e4e…`, `889995d0…`) use different build
   recipes; the evidence doc explains each SHA's scope honestly.

5. **Fuzz campaigns: 50,000 cases across five seeds, zero false PASS.**
   Requal seeds 12345 and 67890 (10,000 each, 3,333 ASan-confirmed temporal
   violations per seed), rotating seeds 20260922 and 20260923 (10,000 each),
   and the ledger-start seed 20260924 (10,000). Zero false PASS, zero false
   positive, zero coverage gap, zero wrong failure class, zero harness
   error in every run.

6. **Phase B operators: 35 calibrated mutation operators** (target ≥30) —
   including both historical soundness incidents (#46, #53) as generator
   mechanisms — all verified correct in every campaign run.

7. **Phase B accounting is auditable and inflation-proof.**
   `tests/fuzz/accumulate.py` computes unique campaign cases from
   `(generator_sha256, seed, case index)`, excludes duplicate runs loudly,
   and hard-rejects any report containing a false PASS. Provenance fields
   (`cand_sha256`, `generator_sha256`, `source_commit`) are carried per
   report; a check.sh smoke test covers the accumulator; the seed-20260923
   provenance caveat (HEAD stamp vs. working-tree generator) is documented
   honestly in the release evidence.

8. **Cumulative ledger started clean.** The ledger starts from a
   fresh-clone run of release candidate `0815216` (clean tree verified):
   10,000 unique cases, 1 unique run, zero false PASS, fully
   self-consistent provenance. Nightly rotation continues on merged main.

9. **Phase B cumulative progress: 10,000 provenance-clean cases of the
   ≥1,000,000 target (1%), zero false PASS to date.** At the current
   nightly rate (~3×10⁴ scheduled cases/run plus manual batches), the
   target is a multi-month accumulation; the gate is defined and the
   accounting now measures it exactly.

10. **Phase C CVE replay: 2/10 intermediate target, both entries green.**
    CVE-2026-87933 (cJSON `merge_patch` UAF) and CVE-2026-50219 (libexpat
    handler-reentry UAF, fixed in 2.8.2): ASan-confirmed on the vulnerable
    revision, clean on the fix, C& classification BOUNDED-INCOMPLETE both —
    defect-path obligations, no false PASS. MISSED is wired as a loud
    soundness incident. Registry v2 supports per-entry build commands with
    `{clone}` placeholders.

11. **CVE pipeline defect found and fixed during pre-merge review.**
    `cand check --contracts` accepts exactly one file (a repeated flag is
    last-wins), so the harness silently loaded only `libc-borrow.yaml`.
    The engine now merges both reviewed bundles into one per-run file; both
    entries were re-validated under the corrected configuration —
    identical classifications and identical obligation counts (697 /
    1,459). The same verification covered the Hiredis Pareto measurement
    (identical: 915 obligations, 4 findings).

12. **Next CVE candidates queued:** libtiff CVE-2025-8176 (fix `fe10872e`,
    off-by-one in `quant_fsdither` — deferred only because the crafted
    multi-row TIFF driver was too heavy to risk pre-release) and libxml2
    CVE-2025-12863 / CVE-2025-49794 / CVE-2026-6653.

13. **Adoption Pareto rebased at function level on exact head.** Hiredis @
    `33a12fb`, 179 functions: 17 CLEAR / 162 BLOCKED, 4 rule-correct
    borrow-escape findings, 915 obligations. Sole-blocker families:
    cross-TU 26, external-API 23, alias/storage 6. Full cross-TU would
    project CLEAR 17→43 — superseding the earlier "~+3" estimate — but
    #42's evidence gate (≥2 additional pilots) is still unmet, by design.

14. **#40 disposed of honestly:** closed with evidence (the parameter
    portion was satisfied by ADR-0024: `unmodelled-pointer-parameter`
    674→0, 2 residual obligations, 0 functions solely blocked). The
    alias/storage remainder was split into a new, evidence-backed #54 with
    guardrails (only 6 functions solely blocked — precision, not a
    dominant blocker).

15. **Issue states:** #53 and #40 closed; #54 open (alias/storage
    precision); #41 and #42 refreshed with exact-head evidence comments;
    #39 (annotation propagation), #36 (pilot program), #25 (P2.1 borrow
    precision) remain open and valid. No preselection of #42 as next work.

16. **External adversarial challenge format landed** (PR #57): submission
    corpus (single ordinary-C11 `case.c` + `cand.challenge/v1` manifest),
    acceptance definition (in-scope SPEC-0010 violation, ASan-confirmed at
    runtime, authoritative PASS with zero findings/obligations), five-step
    triage, incident-class handling with credit — explicitly not a bounty.

17. **Release discipline held.** Every PR to protected main (#55, #56,
    #57) carried the `contract-and-compatibility` gate, an approving review
    from `senolcolak`, and a pre-merge review with findings fixed before
    merge (the contracts-loading bug, CHANGELOG stale-`Unreleased` cleanup
    plus a new check.sh structure guard, evidence provenance honesty,
    workdir hardening). Merge-commit semantics preserved throughout; both
    post-merge main CI runs green.

18. **What v0.2.1 claims — and does not.** Public C&1/v1 scope is
    unchanged: no C&2, no spatial/concurrency claims, no cross-TU
    ownership, no callback-retention, no `realloc` guarantee. Unsupported
    semantics fail closed to INCOMPLETE. The release is a soundness patch
    plus proof infrastructure, nothing broader.

19. **Recommended next engineering milestone (exactly one): reviewed
    external-API contract-bundle expansion (contract ergonomics).**
    - *Evidence:* external-API effects touch the most functions of any
      blocker family in Hiredis (101 containing, 23 solely blocked); the
      needed borrow/no-escape and borrowed-return declarations are
      expressible in the existing contract format — **zero verifier
      semantic change**, so no requalification of analyzer semantics is
      required (only bundle review + red-team fixtures, the exact trust
      model already proven by `libc-borrow.yaml`).
    - *Benefit:* the largest immediately measurable decidability gain per
      unit of soundness risk; directly serves real adoption and the
      CVE-replay program (both replayed defect vectors sit on unmodelled
      external boundaries).
    - *Risk:* low and bounded — a wrong contract entry can only unsoundly
      *suppress* obligations for that symbol, mitigated by per-symbol
      review, red-team fixtures, and fail-closed defaults; incidents remain
      impossible to hide behind the fuzz ledger.
    - Explicitly *not* recommended now: #42 cross-TU (largest raw payoff,
      26 sole-blocked functions, but its own evidence gate — ≥2 additional
      pilots — is unmet and it widens the proof surface) and #54 alias
      precision (only 6 sole-blocked functions).

20. **Redis GO/NO-GO: NO-GO.** The Hiredis pilot (Redis's own C client)
    shows a 9.5% CLEAR rate (17/179) with temporal obligations concentrated
    on unmodelled external and cross-boundary APIs — the exact surfaces the
    recommended milestone addresses. Starting a Redis-server pilot now
    would measure noise, not signal. Re-evaluate after the external-API
    bundle expansion lands and the pilot Pareto is re-measured; the
    decision then rests on measured CLEAR-rate movement, not on estimate.

## Bottom line

The C&1/v1 claim is again defensible — suspended on v0.2.0, repaired,
exactly requalified, and released as v0.2.1 with incident-derived regression
mechanisms, auditable campaign accounting, a growing CVE replay corpus, and
a measured adoption path whose single next step (external-API contract
ergonomics) is chosen on evidence, not appetite.
