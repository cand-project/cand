# C& Safety Claims and Non-Claims

This document defines how C& safety statements are to be interpreted by maintainers, reviewers, users, and automated agents.

It is intentionally conservative. Marketing language, examples, prompt output, sanitizer results, or a green CI run must not expand the safety claim beyond what is stated here and in the normative ADR/SPEC set.

## Current status

The `v0.2.1` release's C&1/v1 claim is **suspended** following incidents
[#62](https://github.com/cand-project/cand/issues/62) and
[#64](https://github.com/cand-project/cand/issues/64). Incident #62: a
pointer parameter reassigned in the body (`p = r; return p;`) had its
returned borrow origin attributed to the syntactically referenced parameter
instead of the parameter whose object was actually returned. Incident #64:
compound return expressions resolved their borrow origin from the **first
parameter contained anywhere in the expression** (`return c ? a : b;`,
`return (first(a), b);`, value reads `return p->f;`/`return p[i];` out of
parameter storage, and call-argument containment `return dupit(p);`), so
callers that destroyed the true origin and used the returned pointer
received an authoritative PASS on confirmed (ASan-verified) use-after-frees.
Both defects are confirmed on the immutable `v0.2.1` tag (the tag is not
rewritten or retagged); its qualification evidence remains available but
must not be cited as a current soundness claim.

The `v0.2.0` release's C&1/v1 claim remains **suspended** from incident
[#53](https://github.com/cand-project/cand/issues/53): tracked pointers passed
at variadic argument positions were skipped by the direct-call summary path
and could receive an authoritative PASS with zero obligations.

The `v0.2.2` release repairs both defects — the #62 repair (fail-closed
`Unknown` return effect when the resolved borrow-origin parameter is
assigned anywhere in the body, ADR-0025, merged as `4627363`) and the #64
repair (whitelist single-parameter origin resolution, ADR-0026, merged as
`928d9bc`) — and carries the milestone #61 conditional borrow/none effect
join (ADR-0027), a precision improvement inside the existing claim's
semantics that adds no new claim. The current qualified release identity is
**`v0.2.2`** (qualified semantic source `cc8e9bd`, exact-head
requalification evidence in
[the v0.2.2 release evidence](CAND1-V0.2.2-RELEASE-EVIDENCE.md)). The
`v0.1.0` tag remains suspended from incident
[#46](https://github.com/cand-project/cand/issues/46). Issue #25 remains
separate and open. This document never claims that arbitrary C accepted by
`cand` is generally memory-safe.

The founding implementation focuses on temporal heap-lifetime reasoning and on one architectural invariant:

> **If C& encounters ownership/lifetime semantics inside its checked scope that it cannot soundly model, the result must be `INCOMPLETE`, not `PASS`.**

This is the Trustworthy PASS rule defined by ADR-0010.

## Verdict semantics

| Verdict | Meaning |
|---|---|
| `PASS` | no known violation and no unresolved ownership/lifetime obligation within the implemented checked scope |
| `FAIL` | C& established a modeled violation |
| `INCOMPLETE` | C& encountered relevant semantics it cannot currently prove |
| tool/input error | no C& safety verdict was produced |

A `PASS` is always relative to:

- the exact C& version/commit;
- the exact frontend/toolchain semantics used for analysis;
- effective compilation flags and preprocessing environment;
- the implemented safety level/profile;
- checked scope;
- trusted contracts and their versions/digests;
- explicit unsupported/unsafe regions;
- proof-policy configuration.

Removing any of that context can turn a precise result into a misleading claim.

## What the current founding phases are trying to establish

The current analyzer is building toward sound temporal ownership reasoning for C. Early rules include direct and flow-sensitive detection of use-after-destruction and double destruction for modeled heap objects.

The engineering sequence is deliberately staged:

1. recognize direct heap lifetime transitions;
2. make unknown semantics fail closed;
3. model control-flow joins and loops conservatively;
4. model storage identity and aliases;
5. model function boundaries through verified summaries/contracts;
6. add explicit ownership/move semantics;
7. add borrow/lifetime rules;
8. define and meet a formal C&1 release gate.

Passing an early stage does not imply the later guarantees.

## C&1/v1 qualified release claim

C&1/v1 provides qualified temporal ownership and borrow safety only for
ownership/lifetime operations fully analyzed within the declared checked scope,
supported semantic subset, and qualified Ubuntu 24.04 x86_64/C11 toolchain
profile. An authoritative C&1 PASS means no covered temporal
ownership/borrow violation was found. Unsupported or unresolved
ownership/lifetime semantics fail closed and cannot contribute to PASS.

Within that boundary, the claim covers:

- ownership creation and transfer;
- move/use-after-move;
- destruction and double destruction;
- use-after-destruction;
- destruction through invalid/non-owning paths where the model defines ownership authority;
- borrow lifetime relationships;
- escaping borrows;
- conflicting borrow modes where specified;
- ownership-affecting external calls through trusted contracts;
- explicit accounting of unsupported or unsafe boundaries.

C&1/v1 is not a claim about arbitrary C or whole-language memory safety. Its
release identity is the exact reviewed commit and its replayable evidence.

## Explicit non-claims

Unless a later safety level or release explicitly says otherwise, C& does **not** claim general protection from:

- array or object bounds violations;
- null dereference;
- integer overflow or integer safety;
- arbitrary pointer arithmetic correctness;
- pointer/integer provenance reconstruction;
- type confusion outside modeled ownership semantics;
- data races or general concurrency safety;
- lock-order/deadlock safety;
- signal-safety errors;
- inline assembly behavior;
- compiler bugs;
- hardware faults;
- undefined behavior unrelated to the claimed ownership/lifetime rules;
- correctness of code in explicit unsupported/unsafe regions;
- correctness of untrusted or incorrect external contracts;
- logical/application correctness.

A clean C& result is not a substitute for ordinary testing, fuzzing, sanitizers, static analysis, compiler warnings, code review, or security review.

## Trusted and untrusted inputs

The trusted computing base must remain explicit.

Potential trusted inputs include:

- the `cand` implementation;
- the Clang AST/CFG behavior relied on by the supported frontend version;
- the exact analysis configuration;
- accepted external ownership contracts;
- accepted declaration-annotation review manifests (`cand.annotation-review/v1`,
  ADR-0029): a manifest is a trusted input only after review, and only
  through the policy pin list; it never trusts the annotations themselves;
- safety-policy configuration used to define checked scope.

The following are **not trusted merely because they exist**:

- LLM-generated code;
- LLM-generated annotations;
- LLM-generated fixes;
- LLM-generated contracts;
- LLM-generated annotation-review manifests (a manifest is trusted only
  after review and policy pinning, never by its existence);
- comments/documentation claiming ownership behavior;
- inferred contracts that have not passed the required trust path;
- baseline/suppression files;
- sanitizer-clean executions.

An LLM may propose a contract, but must not be able to make its own code pass by silently promoting that contract into the trusted base.

## Evidence hierarchy

C& uses several kinds of evidence with different meanings.

### Normative

- accepted ADRs;
- normative SPECs;
- machine-readable contracts/schemas;
- implementation behavior;
- executable regression tests.

### Supporting / independent oracles

- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- Clang Static Analyzer;
- fuzzers;
- external static analyzers;
- independent reviewer test corpora.

These are valuable for finding C& bugs. They are not themselves the definition of C& soundness.

## False PASS policy

A false PASS is the most serious semantic defect in the project.

A suspected false PASS should be classified carefully:

1. Did the defect occur inside semantics C& claimed to model?
2. Did C& encounter the operation but silently ignore it?
3. Should the correct result have been `FAIL` or `INCOMPLETE`?
4. Does the defect invalidate a published safety claim or evidence artifact?

If a false PASS can undermine a published safety guarantee, treat it as potentially security-sensitive and follow `SECURITY.md`.

## False FAIL vs INCOMPLETE

A false `FAIL` is a verifier bug: C& is asserting a known violation that does not exist.

An `INCOMPLETE` result is different. It says that C& does not yet have enough sound semantics to prove the code.

During the founding phases, an overly conservative `INCOMPLETE` is preferable to guessing. The project should reduce unnecessary INCOMPLETE results only when doing so preserves the meaning of PASS.

## Annotation non-interference

C& annotations are intended to carry analysis semantics, not change executable C behavior.

Unless explicitly documented by a future extension, C& metadata must not change:

- ABI;
- data layout;
- calling convention;
- ordinary C runtime behavior;
- generated-code semantics when compiled with the ordinary production compiler.

If bypassing `cand` changes whether a runtime bug exists, that is not the intended C& architecture.

## Requirements before any C&1 soundness claim

Before a release may advertise C&1 as an implemented safety guarantee, the project must have a separately reviewed gate that at minimum defines:

- precise formal/informal semantics for every covered rule;
- supported C language/toolchain subset;
- trusted computing base;
- external contract trust model;
- checked-scope rules;
- unsafe/unsupported accounting;
- deterministic evidence format;
- differential/adversarial corpus requirements;
- fuzz/property-testing requirements;
- compiler/version matrix;
- known limitations;
- independent review expectations;
- versioned evidence for the exact release.

The qualified claim remains limited to the exact evidence and policy boundaries
defined by SPEC-0010; it does not broaden the explicit non-claims below.

## Reviewer rule

When wording is ambiguous, interpret the narrower claim.

> **C& should earn broader safety claims by executable evidence and reviewed semantics, never by implication.**
