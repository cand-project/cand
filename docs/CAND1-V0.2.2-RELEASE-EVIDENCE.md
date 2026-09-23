# C&1/v1 v0.2.2 Release Evidence

Status: **release candidate — metadata-only finalization from fully qualified
protected main**.

## Release identity

- Release version: `0.2.2`
- Qualified semantic source: protected main
  `cc8e9bd1e1794e08e952f2fcd0db082fdb4699dc` (merge of PR #66), requalified
  exactly at this head for this release (evidence below).
- Semantic content relative to v0.2.1: PR #63 (incident #62 repair —
  fail-closed `Unknown` return effect when the resolved borrow-origin
  parameter is assigned anywhere in the body, ADR-0025), PR #65 (incident
  #64 repair — whitelist single-parameter origin resolution for compound
  return expressions, ADR-0026), and PR #66 (milestone #61: the Gate A
  census/counterfactual evidence and the conditional borrow/none effect
  join, ADR-0027 — a precision improvement inside the existing claim's
  semantics that adds no new claim). This release branch adds only
  metadata, documentation, and version pinning; no `src/` verifier source
  changes beyond the qualified head.
- Release candidate source: this exact metadata candidate is based on the
  qualified protected-main source above; its final release commit is the
  protected merge commit recorded with tag `v0.2.2`.
- Previous identities: immutable `v0.2.1` — its C&1/v1 claim is **suspended**
  by incidents [#62](https://github.com/cand-project/cand/issues/62) and
  [#64](https://github.com/cand-project/cand/issues/64) (confirmed
  borrow-origin misattribution false PASSes; the tag is not rewritten or
  retagged and its historical evidence remains available in
  [the v0.2.1 release evidence](CAND1-V0.2.1-RELEASE-EVIDENCE.md)).
  Immutable `v0.2.0` remains suspended from incident #53; immutable
  `v0.1.0` remains suspended from incident #46.

## Incident closure

Incident
[#62](https://github.com/cand-project/cand/issues/62): on v0.2.1 and
earlier, a pointer parameter reassigned in the body (`p = r; return p;`)
had its returned borrow origin attributed to the syntactically referenced
parameter instead of the parameter whose object was actually returned, so
callers that destroyed the true origin and used the returned pointer
received an authoritative PASS on a confirmed use-after-free. First-hand
reproduction on the immutable `v0.2.1` tag. The repair (ADR-0025: the
return effect fails closed to `Unknown` when the resolved borrow-origin
parameter is assigned anywhere in the body) landed in PR #63
(`4627363`) and is pinned by the permanent regressions
`tests/interprocedural/reassigned_borrow_origin_incomplete.c`,
`reassigned_borrow_origin_helper_incomplete.c`,
`reassigned_origin_direct_return_uaf.c`, and
`reassigned_via_outparam_incomplete.c`.

Incident
[#64](https://github.com/cand-project/cand/issues/64): on v0.2.1 and
earlier, compound return expressions resolved their borrow origin from the
**first parameter contained anywhere in the expression** —
`return c ? a : b;`, `return (first(a), b);`, value reads
(`return p->f;`, `return p[i];` on `T**`) out of parameter storage, and
call-argument containment (`return dupit(p);`) — inventing
`borrow_from_arg` facts for callers that destroyed the true origin. Four
confirmed (ASan-verified) false-PASS shapes, one confirmed first-hand on
the immutable `v0.2.1` tag binary. The repair (ADR-0026: origins resolve
only from expressions unambiguously derived from a single pointer
parameter — `p`, interior member chains, `&p->f`, `&p[i]`, `p ± n`,
null-joins, comma-on-last; everything else fails closed to `Unknown`)
landed in PR #65 (`928d9bc`) and is pinned by the permanent regressions
`tests/interprocedural/compound_origin_conditional_incomplete.c`,
`compound_origin_condptr_incomplete.c`,
`compound_origin_subscript_incomplete.c`,
`compound_origin_callargs_incomplete.c`,
`compound_origin_comma_uaf.c`,
`compound_origin_controls_safe.c`, and
`compound_origin_controls_detect.c`.

No false PASS of either class remains on this release source (see
qualification evidence; the classes are additionally exercised by the
milestone #61 paired-regression corpus and the five-pilot measurement
campaigns).

## Claim

v0.2.2 carries the existing SPEC-0010 C&1/v1 claim: qualified temporal
ownership and borrow safety only within the declared checked scope,
supported semantic subset, qualified Ubuntu 24.04 x86_64/C11 profile, and
exact evidence and policy boundaries. Unsupported or unresolved
ownership/lifetime semantics cannot contribute to PASS.

The scope is unchanged from v0.2.1's intended scope. This release adds no
C&2, general memory-safety claim, cross-TU ownership guarantee,
callback-retention guarantee, or `realloc` guarantee. It is a
soundness-patch release (both incident classes repaired) that also carries
the milestone #61 precision improvement (ADR-0027): a parameter that is at
most borrowed (or untouched) on every path is at most borrowed overall, so
conditional borrow/no-effect calls no longer collapse summaries to
`Unknown`. That rule strictly reduces INCOMPLETE surface and adds no
claim; its fail-closed complement (conditional consume/destroy and
unresolved effects) is unchanged and pinned by regression.

## Exact-head qualification evidence (protected main `cc8e9bd`)

Environment: Ubuntu 24.04 x86_64, Clang 18.1.3, C11, Ninja, out-of-tree
Release builds; fresh clone of the exact head.

| Gate | Result |
|---|---|
| Exact-head checkout, clean tree | PASS (HEAD `cc8e9bd…`, `git status --porcelain` empty) |
| `git diff --check` | PASS |
| Build A (Release, `clang++-18`) | PASS, verifier SHA256 `eba50c6a88ded5d504f16bbd02ae026f6ca77f7206271306254165093d2fd2e0` |
| Complete CTest | **20/20 PASS, 0 failed** (includes the parameter-lifetime, variadic-argument, reassigned-origin and compound-origin incident corpora, ownership/move, borrow, pointer-transport, CFG/loop/generation, interprocedural/contract, evidence/policy, sanitizer-differential, cross-TU-boundary, and policy-attack suites) |
| `scripts/check.sh` (version metadata, policy/contract/evidence checks, PASS-path drift guard, CVE registry structure, cumulative-accounting smoke test) | PASS |
| Toolchain attack suite (`tests/toolchain/run.sh`) | PASS |
| Two independent reproducibility builds (`tests/toolchain/reproducible.sh`) | **PASS — bit-identical** (`eba50c6a…`, equal to Build A) |
| Independent adversarial corpus (`tests/fuzz/independent.py`) | 100/100 correct (34 CORRECT_PASS, 33 CORRECT_FAIL, 33 CORRECT_INCOMPLETE) |
| CVE replay at exact head (`tests/cve-replay/run.sh`) | 2/2 entries green: CVE-2026-87933 (cJSON) and CVE-2026-50219 (libexpat), both BOUNDED-INCOMPLETE with defect-path obligations, vulnerable revisions ASan-confirmed, fix revisions clean |
| Protected CI on the merge commit `cc8e9bd` | `contract-and-compatibility` success and `C&1 supported toolchain qualification` success (PR #66 checks; clean-room build, complete CTest, toolchain suite, reproducibility) |

Supporting measurement evidence on the same semantic lineage (milestone
#61, five E1 pilots — hiredis, zlib, curl, libgit2, sqlite): the incident
repairs and the precision rule were measured before/after on all five
pilots — findings preserved exactly in every pilot, zero functions lost
clear, +62 functions cleared by ADR-0027, no new false PASS
(destroy-then-call through a decided borrow parameter upgrades from an
obligation to a FAIL detection). See
`docs/pilots/SAME-TU-SUMMARY-PRECISION-PARETO.md` and
`docs/CAND1-61-FINAL-REPORT.md`.

## Campaign evidence

All campaigns below ran the verifier built from the fresh clean checkout
of `cc8e9bd` (Build A above, SHA256 `eba50c6a…`), extended differential
mode, 10,000 cases per seed, each temporal case ASan-executed in its own
process; every run exercises the complete 35-operator mutation suite.

| Deterministic extended differential fuzz, seed 12345 | 10,000 cases: 3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE; **0 false PASS, 0 false positive, 0 coverage gap, 0 wrong failure class, 0 harness error**; 3,333 ASan temporal confirmations; 35/35 mutation operators correct; deterministic JSON |
| Deterministic extended differential fuzz, seed 67890 | 10,000 cases: 3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE; **0 false PASS, 0 false positive, 0 coverage gap, 0 wrong failure class, 0 harness error**; 3,333 ASan temporal confirmations; 35/35 mutation operators correct; deterministic JSON |
| Rotating campaign seed 20260925 (cumulative-ledger run) | 10,000 cases: 3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE; **0 false PASS, 0 false positive, 0 coverage gap, 0 wrong failure class, 0 harness error**; 3,333 ASan temporal confirmations; 35/35 mutation operators correct; deterministic JSON |

### Cumulative ledger

`tests/fuzz/accumulate.py` accepted all three reports above: **30,000
unique campaign cases across 3 unique runs** (0 duplicate runs excluded),
zero false PASS, zero false positive, zero coverage gap, zero wrong
failure class, zero harness error, 9,999 ASan temporal confirmations. The
provenance triple is self-consistent on every run: verifier
`eba50c6a…` (Build A above, the same binary as the reproducibility gate),
generator `95b9eca4…` (the generator merged as PR #55 and unchanged since
— so the seed 12345 and 67890 corpora are identical to the v0.2.1
requal corpora, re-executed against the repaired verifier), source commit
`cc8e9bd…` (the clean checkout the campaigns ran in).

## Required gate summary

- BLOCKER = 0
- HIGH = 0
- confirmed false PASS = 0 (the #62 and #64 classes are repaired and
  pinned by permanent regressions; the historical classes #46/#53 remain
  repaired and pinned)
- reproducibility = true (bit-identical independent builds)

## Release disposition

Per `docs/CAND1-FALSE-PASS-RESPONSE.md`: both incidents are formalized
(#62, #64), the affected v0.2.1 claim is suspended without rewriting the
immutable tag, the repairs are reviewed and merged with required CI,
permanent regressions are in place, and the complete exact-head gate above
is green. The C&1/v1 claim is restored on the v0.2.2 release identity.
