# C& Threat Model

C& is a security-sensitive verifier. Its primary security asset is not process isolation or secret storage; it is the **integrity of the safety verdict and the evidence used to justify that verdict**.

This threat model applies to the founding phases and should evolve with the implementation.

## Assets to protect

C& must protect the integrity of:

- `PASS` / `FAIL` / `INCOMPLETE` classification;
- checked-scope definition;
- safety level/profile;
- trusted external contracts;
- unsafe/unsupported-region accounting;
- source/configuration identity associated with a result;
- diagnostic and evidence determinism;
- proof-policy settings used by automated agents;
- regression expectations used to prevent false safety claims.

A verifier that can be tricked into returning `PASS` by hiding an unsupported ownership effect has failed even if the `cand` process itself was never compromised.

## Actors and failure sources

### 1. Accidental analyzer unsoundness

The most likely founding-phase threat is an implementation bug that silently ignores a C construct carrying pointer/lifetime state.

Examples include:

- aggregate copies;
- union overlap;
- pointer mutation;
- loop-carried aliases;
- globals/statics;
- out-parameters;
- casts/provenance changes;
- callbacks;
- non-local control flow.

**Primary mitigation:** fail closed. Unsupported relevant semantics must become `INCOMPLETE`.

### 2. Malicious or careless code author

A source author may intentionally or accidentally express ownership effects using obscure C syntax that bypasses analysis.

The verifier must assume input source is untrusted and adversarial.

**Mitigations:** adversarial corpus, compiler-AST-driven analysis, conservative fallbacks, independent oracles, explicit unsupported accounting.

### 3. LLM/coding agent optimizing for green CI

An agent may discover that changing policy is easier than repairing code.

Examples:

- add `unsafe`;
- add suppressions;
- reduce safety level;
- shrink checked scope;
- change trusted contract semantics;
- delete failing tests;
- change expected verdicts.

These must be treated as **proof-policy changes**, not ordinary fixes.

**Mitigations:** ADR-0009, policy diffing, zero/default budgets for proof weakening, human approval for trust-boundary changes, evidence tying the result to effective policy.

### 4. Incorrect or malicious external contracts

A false contract can turn an unknown API boundary into an apparently proven one.

For example, declaring a retaining/consuming function as a simple borrow can invalidate temporal reasoning.

**Mitigations:** explicit contract trust classes, review/promotion workflow, source-derived verification where possible, contract digests in evidence, no self-promotion by the generating LLM.

### 5. Build-configuration mismatch

C semantics depend on preprocessing, macros, include paths, compiler extensions, target, and flags. An analyzer may inspect code that is not the same effective program later built for production.

**Mitigations:** compilation database integration, exact effective command capture, evidence binding to configuration/toolchain, supported-version policy, fail closed when semantics cannot be represented faithfully.

### 6. Frontend/compiler semantic divergence

C& uses Clang tooling for analysis while a project may build with GCC or another compiler. Extensions, undefined behavior interpretation, target ABI details, or preprocessing differences may create semantic drift.

**Mitigations:** supported-toolchain matrix, compatibility tests, explicit non-claims, analysis of effective compile commands, no claim that Clang AST equivalence automatically proves GCC semantic equivalence for all extensions.

### 7. Evidence or CI tampering

A result can be misleading if the tested source, expected outputs, policy, contracts, or test corpus are changed together.

**Mitigations:** reviewed pull requests, protected branches/rulesets where available, deterministic machine output, versioned schemas, exact commit identities, separate policy-change reporting, independent reviewer reproduction.

## Security properties sought

### Verdict integrity

For the claimed checked scope:

- modeled violation -> `FAIL`;
- unresolved relevant semantics -> `INCOMPLETE`;
- `PASS` only when no modeled violation or unresolved obligation remains.

### Non-interference

C& metadata should not silently repair or alter ordinary C runtime behavior, ABI, or data layout.

### Trust-boundary visibility

Changes to trusted contracts, unsafe boundaries, suppressions, safety level, or checked scope must be visible as changes to the proof claim.

### Determinism

The same source/configuration/verifier version should produce materially identical machine verdict/evidence output. Nondeterministic ordering must not affect the semantic result.

### Reproducibility

An independent reviewer should be able to reconstruct the analyzed source/configuration and rerun the relevant checks.

## Out of scope for the current threat model

The founding temporal verifier does not claim to protect against every C vulnerability class.

Unless explicitly brought into a later safety level, this threat model does not imply guarantees for:

- spatial/bounds safety;
- null safety;
- data-race freedom;
- integer safety;
- compiler/hardware compromise;
- malicious LLVM/Clang toolchain binaries;
- supply-chain compromise outside repository/release controls;
- logical application correctness.

These may still be security-relevant; they are simply not part of the current C& proof claim.

## Adversarial review strategy

C& should be tested against **pointer-transport mechanisms**, not only textbook memory bugs.

A useful taxonomy includes:

- variable aliases;
- field/array storage;
- aggregate copy and return;
- overlapping union storage;
- `memcpy`/`memmove`;
- pointer arithmetic and mutation;
- dynamic indexing;
- globals/static locals;
- loop-carried values;
- function parameters/returns/out-parameters;
- callback retention;
- atomics;
- varargs;
- casts and pointer/integer round trips;
- non-local control flow;
- inline assembly/compiler extensions;
- cleanup attributes/destructors supplied by extensions.

Every newly discovered false-PASS shape should become a permanent regression test after responsible disclosure/fix.

## Independent oracles

Sanitizers, Clang Static Analyzer, fuzzers, other static analyzers, and manual reasoning are encouraged as **bug-finding oracles**.

They are not the C& proof definition. In particular:

- ASan only observes executed paths;
- sanitizer-clean execution is not a proof of safety;
- another analyzer disagreeing with C& is a review signal, not automatically proof that either tool is correct.

## LLM-specific assumption

C& assumes LLM output is untrusted, including generated annotations and proposed contracts.

The core architecture is deliberately asymmetric:

> **LLMs synthesize; C& verifies.**

C& must therefore remain useful even if the generator is buggy, reward-hacking, or adversarial.

## Vulnerability reporting

A suspected flaw that can make unsafe code satisfy a published C& safety claim may be security-sensitive.

Follow [`SECURITY.md`](../SECURITY.md) for disclosure guidance. Public issues are appropriate for unsupported constructs, false positives, usability gaps, and non-sensitive design discussions.

## Threat-model maintenance

Any change that broadens the meaning of `PASS`, adds a trusted input, introduces a new proof-policy escape hatch, or changes the analysis frontend should review and update this threat model as part of the same PR.
