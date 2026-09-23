# C&1/v1 v0.2.2 Final Report

Status: final. Date: 2026-09-23.

**Release state:** `v0.2.2` tagged on the release merge commit (the
release PR for branch `release/v0.2.2`), published at
https://github.com/cand-project/cand/releases/tag/v0.2.2.
The `v0.2.1` tag is untouched at `a362306`.

<!-- RELEASE_STATE_FINAL (tag SHA and release PR number to be recorded at
     publication; replace this paragraph before the release commit) -->

## Report

1. **Incidents #62 and #64 formalized and closed.** The v0.2.1
   borrow-origin misattribution false PASSes — parameter reassignment
   (`p = r; return p;`) and compound return expressions
   (`return c ? a : b;`, comma expressions, value reads out of parameter
   storage, call-argument containment) — were filed as BLOCKERs with
   first-hand reproduction on the immutable `v0.2.1` tag, repaired under
   the incident process (ADR-0025, merged as `4627363`; ADR-0026, merged
   as `928d9bc`), and closed with full qualification chains. They are
   permanently encoded as regression corpora
   (`tests/interprocedural/reassigned_*.c`,
   `tests/interprocedural/compound_origin_*.c`).

2. **Governance repaired without rewriting history.** The `v0.2.1` tag
   was not moved or retagged; its C&1/v1 claim is marked
   SUSPENDED/SUPERSEDED in README, SAFETY_CLAIMS, and the v0.2.1 release
   evidence doc (which retains its explicit suspension header). Same
   treatment preserved for v0.2.0 (incident #53) and v0.1.0 (incident #46).

3. **Exact-head requalification of `cc8e9bd`: all gates green.** Fresh
   clean clone, `git diff --check` clean, CTest 20/20, `scripts/check.sh`
   green (version metadata pinned to 0.2.2), toolchain attack suite PASS,
   independent adversarial corpus 100/100, CVE replay 2/2, protected CI
   green on PR #66.

4. **Reproducibility proven.** Two independent clean builds of `cc8e9bd`
   are bit-identical (verifier SHA256 `eba50c6a…`), matching the
   qualification Build A.

5. **Fuzz campaigns: 30,000 cases across three seeds, zero false PASS.**
   Deterministic requal seeds 12345 and 67890 (10,000 extended
   differential cases each, ASan temporal confirmations) and rotating
   seed 20260925 (10,000 cases, cumulative-ledger run). Every run
   exercises the complete 35-operator mutation suite. Zero false PASS,
   zero false positive, zero coverage gap, zero wrong failure class,
   zero harness error in every run.

   Per-seed verdict distributions (identical across all three runs):
   3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE;
   3,333 ASan temporal confirmations per seed; deterministic JSON on
   every case.

6. **Cumulative accounting is auditable and inflation-proof.**
   `tests/fuzz/accumulate.py` accepted the provenance-carrying ledger run
   (clean checkout of `cc8e9bd`, Build A binary): 10,000 unique campaign
   cases, zero false PASS, provenance triple self-consistent
   (`source_commit cc8e9bd…`, generator and verifier digests recorded in
   the evidence doc).

7. **Milestone #61 shipped in this release.** The conditional borrow/none
   effect join (ADR-0027) is a precision improvement inside the existing
   claim's semantics: measured +62 cleared functions across the five E1
   pilots with findings preserved exactly and zero lost-clear, plus 431
   additional decided same-TU summaries. Its fail-closed complement
   (conditional consume/destroy, unresolved callees, branch-divided
   borrow/destroy) is unchanged and pinned by six permanent paired
   regressions. No new safety claim is added by it.

8. **Claim scope unchanged.** v0.2.2 carries the SPEC-0010 C&1/v1 claim
   with the same boundaries as v0.2.1's intended scope: no C&2, no
   general memory-safety claim, no cross-TU ownership guarantee, no
   callback-retention guarantee, no `realloc` guarantee. It is a
   soundness-patch release that also carries a measured precision
   improvement.

9. **The C&1/v1 claim is restored on the v0.2.2 release identity.** With
   all three prior release tags suspended and the current qualified
   identity "none" since incident #62, this release re-establishes a
   qualified claim at exact head `cc8e9bd` with complete replayable
   evidence (`docs/CAND1-V0.2.2-RELEASE-EVIDENCE.md`).

10. **Post-release state.** Open work by measured evidence: milestone #54
    (alias/storage precision), #41 (pointer-output contracts, requires
    scope expansion), #39 (annotation propagation), #36 (real-project
    pilot qualification), #25 (last-use precision). The residual same-TU
    precision debt after milestone #61 (H11ind, H11xtu, H12, H5, H16) is
    catalogued in `docs/CAND1-61-FINAL-REPORT.md` for the next
    milestone's Gate A.
