# ADR-0002: Differential Safety Evidence and Proof Corpus

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** safety claims, tests, CI evidence, release evidence

## Context

C& exists to make ownership and borrowing rules machine-checkable in ordinary C projects. A convincing safety claim needs more than a checker that prints diagnostics on hand-picked examples. The project must demonstrate all of the following separately:

1. the underlying program is valid enough C that ordinary production compilers accept it;
2. the unsafe program still contains the original temporal memory problem when C& enforcement is absent;
3. C& identifies the ownership violation before the ordinary build is accepted;
4. a corrected program is accepted by C& and remains valid ordinary C;
5. adding C& metadata by itself does not silently repair runtime behavior;
6. the evidence is reproducible in CI and tied to stable diagnostic IDs.

Dynamic tools such as AddressSanitizer are useful independent oracles for executed use-after-free, double-free, invalid-free, and related defects. They do not prove absence of a bug, because they only observe executed paths. Conversely, a static ownership checker can reject unexecuted bad paths but may have false positives or unsupported constructs. The two forms of evidence are complementary.

## Decision

C& SHALL use **differential evidence** for every published C& safety level.

For C&1, each diagnostic family SHALL have a proof fixture with an unsafe case and, where meaningful, a corrected case.

The canonical evidence matrix is:

| Stage | Unsafe fixture | Corrected fixture | Purpose |
|---|---|---|---|
| ordinary GCC/Clang compile | must compile for applicable cases | must compile | proves this is a C semantic/safety problem, not merely a syntax error |
| normal execution | may appear to work or fail | must behave as expected | demonstrates why runtime observation alone is insufficient |
| ASan/LSan or other independent runtime oracle | should detect deterministic runtime defects when applicable | must be clean | independent evidence for executed temporal bugs |
| `cand check` | must reject with expected stable diagnostic | must pass | C& prevention evidence |
| ordinary production build after C& pass | not reached in strict enforcement | must build normally | proves C& remains a pipeline gate, not a code generator |

A release SHALL NOT claim C&1 conformance merely because runtime sanitizers pass. A release SHALL NOT claim C&1 conformance merely because C& emits no finding. The checked scope, supported constructs, trusted contracts, and expected diagnostic coverage must all be known.

## Three forms of proof

### 1. Existence proof: ordinary C still has the bug

For a deterministic use-after-free example, the proof sequence is:

```text
unsafe C source
    |
    +--> clang/gcc -Wall -Wextra -Werror --> accepted
    |
    +--> ASan execution ------------------> heap-use-after-free
```

This demonstrates that normal compiler acceptance is not a temporal-memory-safety proof.

### 2. Prevention proof: C& rejects before production compilation

Once the C&1 analyzer implements the relevant rule:

```text
same ownership bug
    |
    +--> cand check --level cand1 --strict
            |
            +--> CAND-T002 use-after-destroy
            |
            X production gate does not open
```

C& does not need to mutate the binary to prevent the checked program from being accepted. The prevention mechanism is **static rejection at the build/CI gate**.

### 3. Repair proof: corrected code passes both static and runtime evidence

```text
corrected C
    |
    +--> cand check ----------------> PASS
    +--> clang/gcc -----------------> PASS
    +--> ASan regression execution -> PASS
```

The repair may have been written by a developer or produced by an explicitly approved fix-it. C& proof concerns the resulting program, not who authored the edit.

## Required C&1 proof families

The initial proof corpus SHALL cover at least the stable temporal diagnostics defined by `contracts/diagnostics.yaml`:

- `CAND-T001` use-after-move;
- `CAND-T002` use-after-destroy;
- `CAND-T003` double-destroy;
- `CAND-T004` destroy-non-owner;
- `CAND-T005` owner-destroyed-with-live-borrow;
- `CAND-T006` owner-lost;
- `CAND-T007` borrow-escapes-owner;
- `CAND-T008` conflicting-mutable-borrow;
- `CAND-T009` unknown-ownership-boundary;
- `CAND-U001` unsupported construct fail-closed behavior.

Not every diagnostic has a useful dynamic oracle. For example, `use-after-move` is a C& ownership-state violation and the underlying C program may still happen to use a valid pointer. In such cases, the proof corpus SHALL distinguish **ownership-contract violations** from **direct runtime memory faults**.

## Runtime oracle policy

AddressSanitizer/LeakSanitizer MAY be used as independent evidence for deterministic fixtures because they detect classes including use-after-free, double-free, invalid free, and leaks on executed paths.

Runtime sanitizers SHALL NOT be treated as the C& definition of correctness.

The proof harness SHOULD:

- compile the unsafe fixture with ordinary warning flags first;
- compile a second instrumented binary with ASan/LSan where applicable;
- require the expected sanitizer class, not merely any crash;
- compile/run the corrected fixture and require a clean result;
- keep sanitizer evidence separate from C& diagnostics.

## External benchmark evidence

After the minimal repository corpus is stable, C& SHOULD add benchmark suites in increasing order of cost:

1. hand-written minimal positive/negative fixtures;
2. CERT/Juliet temporal-memory-safety cases where licensing and automation permit;
3. selected real-world open-source regressions reduced to minimal reproductions;
4. complete real C projects in observe mode and then checked subsets.

Benchmark reporting SHALL publish false-negative, false-positive/rejection, unsupported, and timeout rates separately. A single aggregate “accuracy” percentage is insufficient for a safety tool.

## Evidence manifest

Each proof case SHOULD have machine-readable metadata containing at least:

```yaml
id: uaf-basic-001
level: cand1
class: temporal-memory-runtime-fault
expected:
  ordinary_compile: pass
  sanitizer: heap-use-after-free
  cand: CAND-T002
  fixed_cand: pass
  fixed_sanitizer: pass
```

Until the analyzer exists for a case, the C& expectation SHALL be marked `pending`, never recorded as passing evidence.

## Release evidence

A release that advertises a safety level SHALL publish or generate an evidence summary containing:

- C& version and commit;
- analysis frontend/toolchain versions;
- proof corpus revision;
- diagnostic coverage by ID;
- checked-pass count;
- expected-reject count;
- unexpected accept count;
- unexpected reject count;
- unsupported count;
- trusted contract-set digest/version;
- sanitizer-oracle results where applicable.

An unexpected accept of a fixture that is required to reject SHALL fail the release gate for that safety level.

## Alternatives considered

### Rely only on C& unit tests

**Rejected.** Unit tests prove implementation behavior but do not independently demonstrate the underlying C defect or runtime manifestation.

### Rely only on sanitizers

**Rejected.** Sanitizers are path-dependent runtime detectors, not compile-time proofs of absence.

### Use only large benchmark suites

**Rejected.** Large suites provide breadth but are poor at explaining exactly which semantic rule failed. Minimal fixtures remain normative.

### Claim safety from compiler warnings

**Rejected.** Ordinary C compilers may accept temporal memory bugs cleanly even under strong warning sets.

## Consequences

### Positive

- safety claims become reproducible rather than rhetorical;
- ordinary-C behavior is demonstrated independently;
- C& failures can be mapped to precise diagnostic contracts;
- regressions become visible as unexpected accepts/rejects;
- sanitizers remain useful without being confused with the proof model.

### Negative

- the test matrix is larger than ordinary unit testing;
- some ownership violations have no deterministic runtime oracle;
- real-world benchmarks require reduction and licensing care;
- proof corpus maintenance becomes a release responsibility.

## Invariants

1. **No claim without a negative fixture.** Every enforced rule has at least one case that must reject.
2. **No claim without a positive fixture.** Every enforced rule has safe code that must remain accepted.
3. **No hidden repair.** C& metadata alone does not convert unsafe runtime behavior into safe behavior.
4. **Independent oracle where possible.** Dynamic evidence is used for executable temporal faults.
5. **Stable diagnostics.** Proof expectations name diagnostic IDs, not brittle message text.
6. **Unexpected accept is release-critical.** False negatives in normative fixtures block the safety claim.
7. **Unsupported is explicit.** Unsupported code is not counted as checked-safe.

## Acceptance criteria

ADR-0002 is implemented for C&1 when:

1. the repository contains machine-readable paired unsafe/corrected proof fixtures;
2. deterministic use-after-free and double-free fixtures compile as ordinary C but fail under ASan;
3. their corrected counterparts pass ASan;
4. `cand check` rejects each implemented negative fixture with its expected stable ID;
5. `cand check` accepts each supported corrected fixture;
6. CI publishes or verifies a proof matrix;
7. C&1 release evidence fails on any unexpected accept in a normative fixture.
