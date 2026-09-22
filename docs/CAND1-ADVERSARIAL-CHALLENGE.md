# C&1 External Adversarial Challenge

Status: format defined; submission window open on request. This document
defines the corpus format and triage procedure for external adversarial
submissions against the C&1/v1 claim. It is **not a bounty: no monetary
reward is offered or implied.** Accepted findings receive incident-class
handling and public credit.

## The challenge

C&1/v1 claims (SPEC-0010): within the declared checked scope, supported
semantic subset, and qualified profile, `cand check` never emits an
authoritative **PASS** for code that contains a genuine temporal
heap-lifetime violation (use-after-free, double-free) — unsupported or
unresolved semantics fail closed to INCOMPLETE instead.

A **false PASS** is a counterexample to that claim and is the highest-value
finding this project accepts. Two historical false PASSes have already been
found and handled this way (incidents
[#46](https://github.com/cand-project/cand/issues/46) and
[#53](https://github.com/cand-project/cand/issues/53)); each suspended the
claim until repaired and requalified. If you find a third, you are in good
company.

## What counts as an accepted finding

All of the following must hold for the submission to be accepted:

1. **Ordinary C.** A single self-contained C11 translation unit that compiles
   with the pinned toolchain (Clang 18.1.3, `clang -std=c11`) with **no C&
   annotations** and no dependency on anything beyond libc.
2. **In-scope violation.** The program must contain a genuine temporal
   heap-lifetime violation — a use-after-free or double-free of a heap object
   — that is **inside the C&1/v1 checked scope and supported subset**
   (SPEC-0010): the violating statement is in a file passed to `cand check`,
   and the defect does not depend on semantics the claim explicitly excludes
   (callback retention, aggregate/struct-field retention, `realloc`
   lifetime, cross-TU ownership, spatial bounds).
3. **Executable ground truth.** The violation must actually execute: running
   the program under AddressSanitizer (`clang -fsanitize=address`) produces
   an ASan report for the violation (heap-use-after-free or double-free).
   A violation on a path that never runs is not a finding.
4. **The false PASS.** `cand check` on the submission, in the default
   semantic profile with the reviewed contract bundles, returns an
   **authoritative PASS**: verdict `pass`, zero findings, zero obligations
   (exit code 0).

A submission where `cand` returns `fail` is a correct detection. A
submission where `cand` returns `incomplete` is a correct fail-closed
outcome. Neither is a finding — but well-formed non-findings that expose
near-misses are still welcome as corpus contributions.

## Submission format

A directory (or tarball of one) with exactly these files:

```
<handle>-<date>/
  case.c          # the program (ordinary C11, self-contained)
  manifest.json   # submission metadata, schema cand.challenge/v1
```

`manifest.json`:

```json
{
  "schema": "cand.challenge/v1",
  "handle": "optional-pseudonym",
  "violation_kind": "heap-use-after-free | double-free",
  "violation_site": {"file": "case.c", "line": 42},
  "claimed_verdict": "pass",
  "notes": "free-text description of the mechanism"
}
```

No build system, no extra files, no scripts. If the program needs input, it
must embed it or generate it deterministically.

## Triage procedure (what we will run)

1. **Structure check** — `case.c` compiles standalone; `manifest.json`
   matches the schema; the file contains no C& annotations.
2. **Ground truth** — the program is built and run under ASan with the
   pinned toolchain; the reported violation kind and site must match the
   manifest.
3. **Reproduction** — `cand check --format=json` with the reviewed contract
   bundles, from a clean checkout of a tagged release, on the qualified
   profile (Ubuntu 24.04 x86_64, Clang 18.1.3). The verdict, findings, and
   obligations are recorded.
4. **Scope ruling** — per SPEC-0010: if the defect mechanism falls in an
   explicitly excluded class (callback retention, aggregate retention,
   `realloc`, cross-TU, spatial), the submission is closed as
   *out-of-scope*, with the ruling documented.
5. **Verdict** — `pass` + ASan-confirmed violation + in-scope ⇒ **accepted
   false PASS**: incident-class handling per
   [`docs/CAND1-FALSE-PASS-RESPONSE.md`](CAND1-FALSE-PASS-RESPONSE.md)
   (claim suspension, incident issue, repair, requalification, release).
   Submitters are credited in the incident record unless they decline.

Accepted false-PASS submissions are additionally pinned as permanent
regressions and encoded as fuzz mutation mechanisms when a generator
mechanism can express them — the same treatment incidents #46 and #53
received.

## What is out of scope for this challenge

- Spatial memory-safety violations (bounds, overflow) — C&2 territory.
- Violations that require C&-excluded semantics (see the scope ruling
  above) receiving PASS — these are known claim boundaries, not defects.
- Verifier crashes, hangs, or diagnostics-quality complaints — please
  report them as ordinary issues; they are not false PASSes.
- Anything requiring annotations, custom contracts, or non-default
  profiles: the challenge runs the ordinary-C path only.

## Relationship to the internal campaign

This challenge is the external leg of proof-plan Phase B, item 4. The
internal differential campaign (mutation operators, ASan-confirmed known
violations, CVE replay) is the primary soundness evidence; the challenge
exists because adversaries choose mechanisms generators do not. Every
submission, accepted or not, is triaged and the outcome recorded.
