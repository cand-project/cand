# ADR-0006: Ownership Inference and Trust Hierarchy

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** ownership facts, inference, contracts, AI-assisted migration, strict proof

## Context

C source does not explicitly encode enough ownership information to recover every programmer intention. C& must therefore combine explicit metadata, known allocator/destructor semantics, external API contracts, interprocedural analysis, and inference.

If all inferred facts are treated equally, the checker can become unsound: a plausible but incorrect guess about whether an API borrows, consumes, retains, or returns ownership can make unsafe code appear safe.

On the other hand, requiring every pointer and function to be manually annotated would make adoption prohibitively expensive.

## Decision

C& SHALL define a **trust hierarchy** for ownership facts and SHALL distinguish facts that may support a strict safety proof from facts that are merely migration suggestions.

The initial hierarchy is:

```text
highest proof authority

1. normative C& semantics / built-in verified primitives
2. explicit source annotations in checked code
3. reviewed trusted external contracts
4. sound facts derived from analyzed function bodies
5. conservative interprocedural inference
------------------------------------------------ proof boundary
6. heuristic inference
7. documentation/name-pattern inference
8. AI/LLM-generated suggestions

lowest authority
```

Levels below the proof boundary MAY help generate candidate annotations/contracts, but SHALL NOT directly satisfy a strict C&1 proof until promoted through review or independently verified analysis.

## Trusted fact sources

### Normative built-ins

C& MAY ship reviewed built-in models for foundational APIs such as allocation/deallocation primitives.

These models are versioned proof inputs and are subject to the same review/evidence requirements as other contracts.

### Explicit source annotations

Annotations such as `CAND_OWN`, `CAND_TAKES`, and borrow markers express programmer intent.

They are trusted as **declared contracts**, but the checker must still verify that the implementation and uses are consistent with them where implementation bodies are available.

An annotation is not a waiver from analysis.

### Reviewed external contracts

Machine-readable contracts model APIs whose source cannot or should not be modified.

A contract becomes trusted only through an explicit review/acceptance process. Provenance and version/digest SHOULD be available in evidence output.

### Body-derived facts

Where function implementation is available, C& SHOULD derive and verify effects such as:

- allocation/returned owner;
- destruction;
- ownership transfer;
- borrow returned from an argument/object;
- retention/escape;
- out-parameter production;
- conditional ownership behavior.

Derived summaries can support proof only when the analysis used to derive them is within the supported/sound profile.

## Conservative inference

When C& cannot prove one unique ownership interpretation, strict analysis SHALL choose a conservative result or reject the ambiguity rather than choose the interpretation most likely to make the program pass.

Examples:

- unknown external call may retain a pointer -> require contract/unsafe boundary;
- ambiguous alias may outlive owner -> reject or mark unsupported;
- uncertain destructor family -> do not assume `free` compatibility;
- uncertain returned-pointer lifetime -> do not assume owned.

## Heuristic inference

C& MAY use names, signatures, documentation, common conventions, or observed tests to suggest likely semantics.

Examples:

```text
*_new / *_create  -> candidate returns-own
*_free / *_destroy -> candidate destroys parameter
*_get / *_view     -> candidate borrowed return
```

These are useful migration accelerators but are not proof facts.

The UI/report MUST visually distinguish suggested facts from trusted facts.

## AI-assisted inference

AI/LLM assistance MAY be used to:

- propose ownership annotations;
- propose external contracts;
- summarize likely owner/borrow relationships;
- explain diagnostics;
- suggest tests that would validate a contract;
- prioritize ambiguous APIs for human review.

AI-generated facts SHALL be untrusted by default.

Promotion path:

```text
AI/heuristic suggestion
       |
       v
candidate contract/annotation
       |
       +--> human review
       |       and/or
       +--> body/evidence verification
               |
               v
          trusted proof input
```

A confidence score is not a substitute for proof authority.

## Contradiction handling

If two trusted sources disagree, C& SHALL fail closed and report the contradiction.

Example:

```text
header annotation: parameter borrowed
external trusted contract: parameter consumed
```

This is a configuration/model error, not an opportunity for the analyzer to choose one silently.

Precedence MAY help identify which input should be corrected, but SHALL NOT hide a trusted contradiction.

## Contract provenance

Trusted contracts SHOULD record:

- schema version;
- package/library identity;
- library version range;
- source/reference used to establish semantics;
- reviewer/approval metadata where project policy permits;
- content digest;
- generated-vs-authored origin.

Evidence reports SHOULD include the digests of trusted contract sets used for a safety claim.

## Inference feedback loop

Observe mode MAY use runtime tests, sanitizer traces, source analysis, and developer corrections to improve candidate models.

However, runtime observation that an API did not free/retain a pointer in observed tests does not prove that it never does so on another path.

Dynamic evidence can refute a candidate model but generally cannot establish universal ownership semantics by itself.

## Alternatives considered

### Require all ownership annotations manually

**Rejected.** Sound but adoption cost would be excessive for mature codebases.

### Trust high-confidence AI inference

**Rejected.** Confidence is probabilistic and can create false safety claims.

### Trust naming conventions

**Rejected.** C APIs are inconsistent and frequently violate naming expectations.

### Infer everything from implementation bodies

**Rejected as universal policy.** Source may be unavailable; analysis may encounter unsupported constructs; public API contracts are useful even when implementations exist.

## Consequences

### Positive

- C& can automate migration without confusing guesses with proof;
- strict mode remains conservative;
- AI assistance can be useful without entering the trusted computing base;
- contract provenance makes safety evidence reproducible;
- ambiguous APIs become visible work items.

### Negative

- users may need to review many candidate contracts early in adoption;
- strict mode will reject some valid code until semantics are modeled;
- proof authority/provenance metadata adds complexity;
- summary derivation must itself have a well-defined soundness profile.

## Invariants

1. **Probabilistic inference is never proof authority by itself.**
2. **Trusted contradictions fail closed.**
3. **Unknown ownership effects inside strict scope fail closed or cross explicit unsafe.**
4. **Source annotations express contracts but do not disable verification.**
5. **Trusted external contracts are versioned proof inputs.**
6. **Heuristics and AI remain visibly untrusted until promoted.**
7. **Evidence reports can identify the contract/model set behind a claim.**

## Acceptance criteria

ADR-0006 is implemented when:

1. the semantic IR records fact provenance/trust class;
2. strict checking refuses to prove a path using heuristic-only ownership facts;
3. candidate contracts can be generated without becoming trusted automatically;
4. trusted contract contradictions produce a deterministic error;
5. evidence output lists trusted contract/model versions or digests;
6. observe-mode reports distinguish inferred/suggested ownership from proven/trusted ownership.
