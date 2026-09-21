# C& Proof Plan — Proving the C&1 Claim

Status: proposed plan; no semantic implementation is changed by this document.
Base: `283a80e7391ba2da47f8740aa1705aacdcf70507` (post-ADR-0024 merge, PR #51).
Normative claim boundary: [SPEC-0010](spec/SPEC-0010-cand1-v1-claim.md).
Invariant (unchanged, absolute): **confirmed temporal violation + C&1 PASS = 0.**

This plan defines what "C& works" means as a set of independently testable
proof obligations, the method and acceptance gate for each, and the campaign
order that assembles them into a single reviewable proof report. A proof
obligation that fails halts its campaign as an incident (per the false-PASS
response process) — no gate may be weakened to pass.

## 1. The claim under test

C&1/v1 is a *narrow* claim. "C& works" decomposes into exactly six
obligations; nothing outside this list is claimed.

| # | Obligation | Informal statement |
|---|---|---|
| C1 | **Soundness (no false PASS)** | If `cand check` returns PASS on ordinary C in checked scope, the program contains no temporal ownership violation within C&'s abstraction. |
| C2 | **Rejection correctness** | Every program containing a sanitizer-confirmed temporal violation is rejected (FAIL or INCOMPLETE) — never PASS. |
| C3 | **Ground truth of findings** | Every emitted FAIL corresponds to a real, runtime-reproducible violation; the false-positive rate is measured, not assumed. |
| C4 | **Decidability on real code** | A measured and growing fraction of real-world C functions receives a definite verdict (PASS or FAIL) rather than INCOMPLETE. |
| C5 | **Determinism and reproducibility** | Identical inputs produce byte-identical outputs on the pinned toolchain, on any machine, forever. |
| C6 | **Thesis validity (agent loop)** | LLM agents emitting C& intent and consuming C& obligations converge on memory-safe code without bypassing the verifier's authority. |

Honest scope limits (non-claims, per release notes): spatial errors, null
dereferences, data races, and out-of-checked-scope behavior are not claimed.

## 2. Where we stand today (baseline, all at the base commit)

| Obligation | Current strongest evidence | Gap to "clear proof" |
|---|---|---|
| C1 | 20,000 differential cases (2 CI seeds), 100 independent-corpus cases, 12 red-team cases: zero false PASS; verifier ASan/UBSan-clean | Bounded corpus; no structural argument; no external adversaries |
| C2 | 6,666/6,666 known-violation cases ASan-confirmed and rejected; 14/14 mutation operators correct | Same bound; no historical-bug replay |
| C3 | Every FAIL fixture ASan-confirmed; every PASS fixture sanitizer-clean; 4 genuine B003 findings + 1 real variadic catch on Hiredis | Findings triage done on one pilot only; one documented false-positive class (alias-through-free) |
| C4 | Hiredis pilot: 674 → 0 dominant blocker class; 887 obligations (−29%); 17/179 CLEAR functions | Single project; 9.5% CLEAR rate; cross-TU missing |
| C5 | Pinned Docker toolchain; byte-identical JSON; all 3 CI workflows green on GitHub runners | No third-party replication protocol published |
| C6 | Agent protocol gated: zero authority bypasses in fuzzing; evidence-bound obligations | No end-to-end agent-loop experiment |

## 3. Campaign order and acceptance gates

### Phase A — Structural soundness argument (C1, semi-formal)

**Method.** Enumerate every code path that can emit a `pass` verdict in
`src/cand.cpp` and demonstrate, path by path, that each is gated by: (a) all
obligations resolved, (b) zero findings, (c) the escape/borrow-retention
obligations that the unknown-call and summary paths emit. Publish as a review
document (`CAND1-PASS-PATH-AUDIT.md`) with a line-referenced inventory of
verdict-emitting sites and the invariant each enforces. Add a CI guard that
fails when the inventory and the code drift (verdict-site count assertion).

**Why it matters.** Empirical corpora can only bound the false-PASS rate from
above; the structural argument is what makes "fail-closed by construction"
reviewable instead of asserted.

**Acceptance gate.** Every `pass`-emitting site inventoried with its gate;
zero ungated sites; independent reviewer sign-off on the audit; CI guard live.

> **Status: complete.** The audit is
> [`docs/CAND1-PASS-PATH-AUDIT.md`](CAND1-PASS-PATH-AUDIT.md): 7 `"pass"`
> literal occurrences forming 4 emitting sites (the semantic verdict; the
> agent-mode cand1 verdict behind `canEmitCand1Pass` and its 8 gate
> conditions; two `policy_result` evidence fields) plus 1 consumer-side
> rejector, and the fail-closed exits that can never emit PASS. The CI guard
> [`scripts/pass-path-guard.sh`](../scripts/pass-path-guard.sh) is wired into
> `scripts/check.sh` and fails on literal-count drift, on any weakening of the
> semantic-verdict ternary or of any `canEmitCand1Pass` gate condition, and on
> loss of the attestation rejector; both weakening vectors were
> negative-tested. Reviewer sign-off happens through the PR that lands the
> audit.

### Phase B — Empirical soundness at scale (C1, C2)

**Method.**
1. Continuous differential fuzzing: nightly extended runs, rotating seeds,
   target **≥10⁶ cumulative generated cases** with ASan cross-validation of
   every known-violation case (current: 2×10⁴).
2. Mutation-operator expansion: extend beyond the current 14 operators
   (target ≥30), including compound mutations and cross-function mutations.
3. Incident-replay suite: every historical false PASS (v0.1.0
   parameter-lifetime incident, variadic-argument incident) permanently
   encoded as generated-corpus mechanisms, not just hand fixtures.
4. External adversarial window: publish the generator and corpus format and
   invite red-team submissions; each accepted false PASS is a paid/incident-
   class finding.

**Acceptance gate.** ≥10⁶ cases with zero false PASS, zero unexplained
false positive, zero harness error; ≥30 mutation operators correct; ≥1
external adversarial cycle completed with all submissions triaged.

> **Status: rotating-seed campaign live.** The nightly
> [`cand1-extended-fuzz.yml`](../.github/workflows/cand1-extended-fuzz.yml)
> now runs three date-derived rotating seeds per scheduled run (~3×10⁴ fresh
> cases per night toward the ≥10⁶ target) in addition to the two fixed
> regression-anchor seeds; cumulative accounting accrues through the run
> artifacts. Mutation-operator expansion (beyond the current 14), the
> incident-replay generator mechanisms, and the external adversarial window
> remain open.

### Phase C — Historical-bug replay (C2, C3 — the decisive external test)

**Method.** Curate a corpus of **≥30 historical temporal memory-safety CVEs**
(use-after-free, double-free) in ordinary C projects with reproducible builds
at the vulnerable commit (image libraries, network daemons, interpreters —
not the kernel initially). For each: check out the vulnerable revision, run
`cand check` on the affected translation units, and record one of:
DETECTED (finding at or covering the defect site), BOUNDED-INCOMPLETE
(obligation covering the defect site, verdict fail-closed), or MISSED
(no obligation covers the defect — a soundness incident if the vulnerable
program otherwise PASSes).

**Why it matters.** Differential fuzzing tests the generator's imagination;
CVE replay tests against defects that actually shipped and hurt people. This
is the closest available analogue to an external oracle.

**Acceptance gate.** ≥30 CVEs replayed; every case DETECTED or
BOUNDED-INCOMPLETE; zero MISSED-with-PASS; per-CVE results published
(machine-readable plus narrative).

> **Status: harness live, 1/30 replayed.** The replay suite is
> [`tests/cve-replay/`](../tests/cve-replay/README.md) (registry + engine +
> CI job [`cand1-cve-replay.yml`](../.github/workflows/cand1-cve-replay.yml),
> weekly and on demand). It validates each entry's ground truth under ASan at
> both the vulnerable revision (must crash) and the fix revision (must be
> clean), classifies the C& verdict, fails loudly on MISSED (false PASS =
> soundness incident) and on expectation drift. First validated entry:
> **CVE-2026-87933** (cJSON heap-use-after-free in `merge_patch`, CWE-416,
> fix DaveGamble/cJSON#1065) — classification **BOUNDED-INCOMPLETE**: zero
> findings, 697 obligations with the free site, the UAF use site, and the
> driver call all carrying obligations. No false PASS.

### Phase D — Decidability demonstration (C4)

**Method.**
1. Pilot expansion from 1 to **≥5 real projects** (diverse domains: a
   database/engine, a protocol library, a parser, a systems daemon, an
   embedded stack), each pinned and measured with the fnmap methodology
   (obligations, CLEAR/BLOCKED functions, class Pareto).
2. Land cross-TU summaries (measured ≈+3 CLEAR upper bound on Hiredis;
   addresses the 55 named same-project escape obligations).
3. Fix the documented alias-through-free false positive (raises C3 as well).
4. Track per-release: CLEAR rate, obligation density, and residual-class
   Pareto, published in the adoption-blocker document.

**Acceptance gate.** ≥5 pilots measured; aggregate CLEAR rate **≥25%** with
zero soundness regressions; every residual obligation class attributable to a
roadmap item; the alias false positive eliminated with a red-team fixture.

### Phase E — Replication and thesis validation (C5, C6)

**Method.**
1. Publish a third-party replication protocol: pinned Docker image digests,
   exact commands, expected byte-level outputs for a checksummed corpus —
   sufficient for an uninvolved party to reproduce every table in the proof
   report.
2. Agent-loop experiment: N ≥ 100 synthesis/repair tasks on ordinary C
   (including seeded temporal-defect repair), run as a controlled comparison —
   agents with C& in the loop vs. agents with compiler+ASan only. Measure:
   residual temporal-defect rate in produced code, convergence (obligation
   burn-down), and authority-bypass attempts (must remain zero by policy
   enforcement, not by honesty).

**Acceptance gate.** One independent third-party replication completed and
signed off; agent-loop defect rate strictly lower in the C& arm; zero
authority bypasses; results published with methodology.

### Phase F — Claim assembly and restoration

**Method.** Assemble `CAND1-V1-PROOF-REPORT.md` from Phases A–E with a
requirement matrix in this repository's qualification-plan format, mapping
each obligation (C1–C6) to its evidence. Independent review (GitHub PR per
merge authority), then restore the public C&1/v1 claim within the SPEC-0010
boundary and flip ADR-0022/ADR-0024 statuses per the decision lifecycle.

**Acceptance gate.** All phase gates green; report merged through protected
main; claim text updated to cite the proof report.

## 4. Falsifiers — what would disprove the claim

Declared in advance, per the project's incident policy:

- Any false PASS in any phase (fuzz, CVE replay, adversarial window, agent
  loop) is a **BLOCKER**: the campaign halts, the claim (if restored) is
  suspended, and the defect enters the incident process.
- A MISSED CVE with an otherwise-PASS verdict is a soundness incident (same
  handling).
- A reproducible determinism failure on the pinned toolchain is a release
  blocker for the claim.
- A failed agent-loop bypass is a protocol incident (policy enforcement bug),
  handled before any thesis claim.

## 5. Sequencing and effort

| Phase | Depends on | Estimated effort | Parallelizable |
|---|---|---|---|
| A — PASS-path audit | — | 1–2 weeks | yes (with B) |
| B — Fuzzing at scale | — | continuous, infra ~1 week | yes |
| C — CVE replay | build-reproducibility tooling | 3–5 weeks | yes (after curation) |
| D — Decidability (5 pilots, cross-TU) | cross-TU implementation | 4–8 weeks | partially |
| E — Replication + agent loop | stable release candidate | 3–6 weeks | yes |
| F — Assembly | A–E gates | 1–2 weeks | no |

Critical path: A + B start immediately; C curation starts immediately
(longest lead time); D's cross-TU work is the largest engineering item; E
follows the stabilized candidate; F closes.

## 6. Relationship to existing governance

This plan changes no claim and no implementation. It extends the existing
C&1-E qualification methodology (paired safe/unsafe evidence, sanitizer
oracles as bug oracles only, exact-head review, protected-main merge
authority) from a release gate into a standing proof program. The invariant
"confirmed temporal violation + C&1 PASS = 0" remains absolute at every
phase; no phase may trade soundness for decidability or adoption.
