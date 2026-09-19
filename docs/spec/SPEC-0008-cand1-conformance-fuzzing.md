# SPEC-0008 — C&1-C conformance and differential fuzzing

Status: Draft release gate; C&1 public claim remains disabled

## 1. Scope

C&1-C makes adversarial verification reproducible. It does not expand C&1
semantics. A fixture is classified before analysis as exactly one of:

* `SAFE`: the source has no intentional modeled temporal violation;
* `KNOWN_VIOLATION`: the source intentionally contains a modeled ownership or
  lifetime defect; or
* `UNSUPPORTED`: the source exercises a fail-closed transport or frontend
  boundary outside the supported profile.

The expected behavior is `PASS` where a `SAFE` fixture is completely supported,
`FAIL` for a supported `KNOWN_VIOLATION`, and `INCOMPLETE` for `UNSUPPORTED`.
An incomplete result is correct safety behavior when the source crosses a
declared boundary; it is not a fuzz failure merely because another analyzer
accepts the source.

## 2. Oracles

C& is the ownership-semantics authority. GCC, Clang, ASan, UBSan, and the
Clang Static Analyzer may provide independent bug-finding evidence, but they
do not define C& ownership semantics. In particular, ASan's lack of a runtime
error does not prove ownership validity. A relevant temporal ASan report plus
`cand1 PASS` is an immediate false-PASS failure. LeakSanitizer, intentional
non-zero exits, assertions, timeouts, and unrelated sanitizer reports are not
temporal findings.

## 3. Reproducibility

`tests/fuzz/generate.py` is deterministic from seed, generator version,
profile version, and case count. It never uses wall-clock time, process ID,
filesystem iteration order, hostname, or Python hash ordering. The manifest
records every seed, case ID, source, semantic class, mechanism, mutation, and
generator version. The mutation engine applies named semantic operators, not
arbitrary token noise.

Each case has a transformation plan. The manifest is derived from that plan,
and the runner validates the rendered source before analysis. A mechanism is
`exercised` only when its modeled syntax or transport occurs in the analyzed
source; a metadata-only label never contributes to coverage. Reports retain
both `mechanisms_declared` and `mechanisms_exercised`.

## 4. Coverage

The mechanism vocabulary covers allocation/destruction/use, move and borrow
lifetimes, aliases, CFG joins and loops, aggregates and pointer transport,
globals/statics, calls and summaries, compiler extensions, contracts, protocol
inputs, and frontend arguments. Coverage is semantic mechanism coverage, not a
claim of source-line coverage. Generated cases are deduplicated by their
stable case identity and are reported with compiler-valid and result counts.
Generated cases are analyzed individually. A SAFE case using only supported
cand1/v1 semantics must produce the authoritative PASS path; SAFE plus
INCOMPLETE is a COVERAGE_GAP, not a correct result.

ASan evidence is case-level: the corpus may be compiled once, but every case
is dispatched in a separate process. A sanitizer count is reported only for
the case that emitted that class. Leak, timeout, assertion, and unrelated
sanitizer outcomes are not temporal ownership confirmations.

## 5. Differential result vocabulary

The runner distinguishes `CORRECT_PASS`, `CORRECT_FAIL`,
`CORRECT_INCOMPLETE`, `FALSE_PASS`, `FALSE_POSITIVE`, `WRONG_FAILURE_CLASS`,
`COMPILER_REJECTED`, `SANITIZER_DEFECT`, `HARNESS_ERROR`, and `TIMEOUT`.
Verifier crashes and hangs are infrastructure defects, never `INCOMPLETE`.
Every false PASS is minimized by the semantic mutation/reproducer workflow and
persisted under `tests/regressions/false-pass/`; crash reproducers belong under
`tests/regressions/crash/`.

## 6. Mutation and independent qualification

Every named mutation operator is executed through compilation and the strict
cand1 agent path. Structural assertions verify that the intended transport or
CFG transformation occurred. The independent review corpus runs one fresh
source and one analyzer invocation per case; class batching is not independent
evidence.

## 7. Qualification gate

The fast profile uses 1,000 fixed-seed generated cases per pull request. The
extended profile uses at least 10,000 cases and multiple seeds on schedule or
manually. Qualification also requires at least 100 curated conformance
fixtures and 100 independent exact-head review cases. These counts are
engineering gates, not statistical proof of soundness. C&1 remains disabled
until C&1-D toolchain reproducibility is complete.
