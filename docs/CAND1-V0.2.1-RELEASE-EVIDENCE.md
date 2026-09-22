# C&1/v1 v0.2.1 Release Evidence

Status: **release candidate — metadata-only finalization from fully qualified
protected main**.

## Release identity

- Release version: `0.2.1`
- Qualified semantic source: protected main
  `1e1f58297fa54d671afdf9619e436e319e95638a` (merge of PR #52), requalified
  exactly at this head for this release (evidence below).
- Semantic content relative to v0.2.0: PR #51 (ADR-0024 parameter-identity
  completion, variadic false-PASS repair, reviewed libc borrow bundle) and
  PR #52 (C&1 proof-plan infrastructure: PASS-path audit, rotating fuzz
  seeds, CVE replay suite). PR #55 (merge `19abafe`, qualification
  infrastructure only: 35 mutation operators, cumulative campaign
  accounting, CVE replay registry v2 + libexpat entry, CI ninja pin) changes
  no `src/` verifier source either. This release adds no new semantic claim;
  everything beyond `1e1f582` is metadata, documentation, CI repair, and
  qualification infrastructure.
- Release candidate source: this exact metadata candidate is based on the
  qualified protected-main source above; its final release commit is the
  protected merge commit recorded with tag `v0.2.1`.
- Previous identities: immutable `v0.2.0` — its C&1/v1 claim is **suspended**
  by incident [#53](https://github.com/cand-project/cand/issues/53)
  (confirmed variadic-argument false PASS; the tag is not rewritten or
  retagged and its historical evidence remains available in
  [the v0.2.0 release evidence](CAND1-V0.2.0-RELEASE-EVIDENCE.md)).
  Immutable `v0.1.0` remains suspended from incident #46.

## Incident closure

Incident [#53](https://github.com/cand-project/cand/issues/53): on v0.2.0 and
earlier, tracked pointers passed at argument positions beyond a callee's
modelled parameter list (variadic slots) were skipped by the direct-call
summary path and could receive authoritative PASS with zero obligations.
First-hand reproduction on the immutable `v0.2.0` tag (verifier built from
tag `e1ae442`, SHA256
`ce50fb8b167335609fd626f921f2a0b8d42fa4ac5081a3a7710a18a42b5e527e`):
`pass`, 0 findings, 0 obligations for a destroyed tracked pointer passed at
a variadic position. The repair (fail-closed escape/borrow-retention
obligations at every unmodelled argument position) landed in PR #51 and is
pinned by the permanent regression
`tests/interprocedural/variadic_argument_escape.c` plus the
`VARIADIC_ESCAPE_LIVE`/`VARIADIC_ESCAPE_DEAD` fuzz mutation operators.
No false PASS of this class remains (see qualification evidence).

## Claim

v0.2.1 carries the existing SPEC-0010 C&1/v1 claim: qualified temporal
ownership and borrow safety only within the declared checked scope, supported
semantic subset, qualified Ubuntu 24.04 x86_64/C11 profile, and exact evidence
and policy boundaries. Unsupported or unresolved ownership/lifetime semantics
cannot contribute to PASS.

The scope is unchanged from v0.2.0's intended scope. This release adds no
C&2, general memory-safety claim, cross-TU ownership guarantee,
callback-retention guarantee, or `realloc` guarantee. It is a soundness patch
release: the public C&1/v1 scope is unchanged and the v0.2.0 claim defect is
repaired.

## Exact-head qualification evidence (protected main `1e1f582`)

Environment: Ubuntu 24.04 x86_64, Clang 18.1.3, C11, Ninja, out-of-tree
Release builds; fresh clone of the exact head.

| Gate | Result |
|---|---|
| Exact-head checkout, clean tree | PASS (HEAD `1e1f582…`, `git status --porcelain` empty) |
| `git diff --check` | PASS |
| Build A (Release, `clang++-18`) | PASS, verifier SHA256 `6caa27b628706afbe777e4920ae567cbdb0dce474e744eaa8aa4a42f8dbaa74d` |
| Complete CTest | **19/19 PASS, 0 failed** (includes the parameter-lifetime incident corpus, the variadic-argument incident corpus, ownership/move, borrow, pointer-transport, CFG/loop/generation, interprocedural/contract, evidence/policy, and sanitizer-differential suites) |
| `scripts/check.sh` (baseline: policy/contract/evidence checks, PASS-path drift guard, CVE registry structure) | PASS |
| Toolchain attack suite (`tests/toolchain/run.sh`) | PASS |
| Two independent reproducibility builds (`tests/toolchain/reproducible.sh`) | **PASS — bit-identical** (`6caa27b6…`) |
| Independent adversarial corpus (`tests/fuzz/independent.py`) | 100/100 correct (34 CORRECT_PASS, 33 CORRECT_FAIL, 33 CORRECT_INCOMPLETE) |
| Deterministic extended differential fuzz, seed 12345 | 10,000 cases: 3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE; **0 false PASS, 0 false positive, 0 coverage gap, 0 wrong failure class, 0 harness error**; 3,333 ASan temporal confirmations |
| Deterministic extended differential fuzz, seed 67890 | 10,000 cases: 3,334 correct PASS, 3,333 correct FAIL, 3,333 correct INCOMPLETE; **0 false PASS, 0 false positive, 0 coverage gap, 0 wrong failure class, 0 harness error**; 3,333 ASan temporal confirmations |
| Clean-room CI on the merge commit `1e1f582` | `contract-and-compatibility` success and `C&1 supported toolchain qualification` success (pinned Docker reference environment: clean-room build, complete CTest, toolchain suite, reproducibility) |
| CVE replay at exact head (`tests/cve-replay/run.sh`) | 2/2 entries green: CVE-2026-87933 (cJSON) and CVE-2026-50219 (libexpat), both BOUNDED-INCOMPLETE with defect-path obligations, vulnerable revisions ASan-confirmed, fix revisions clean |

Additional campaign evidence at the same semantic source (binary
`0d002e4e26aa91736dcb43a003dbad7f7974f5fc2533ee0bfb8e208d8556044b`, built
from content-identical `src/`):

- Rotating campaign seeds 20260922 and 20260923: 10,000 cases each, zero
  false PASS, zero false positive, zero harness error. Seed 20260923
  exercised the expanded 35-operator mutation suite with all 35 operators
  correct, and is the first provenance-carrying campaign run
  (`generator_sha256`, `cand_sha256`, `source_commit 1e1f582…`) acceptable
  to the cumulative accounting in `tests/fuzz/accumulate.py`.

## Required gate summary

- BLOCKER = 0
- HIGH = 0
- confirmed false PASS = 0 (the #53 class is repaired and pinned by
  regression and mutation operators)
- reproducibility = true (bit-identical independent builds)

## Release disposition

Per `docs/CAND1-FALSE-PASS-RESPONSE.md`: the incident is formalized (#53),
the affected v0.2.0 claim is suspended without rewriting the immutable tag,
the repair is reviewed and merged with required CI, permanent regressions are
in place, and the complete exact-head gate above is green. The C&1/v1 claim
is restored on the v0.2.1 release identity.
