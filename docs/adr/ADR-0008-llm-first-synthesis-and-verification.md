# ADR-0008: C& Is LLM-First — Machine Synthesis, Deterministic Verification

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** product architecture, CLI/protocol design, diagnostics, proof workflow, adoption model
- **Depends on:** ADR-0001, ADR-0002, ADR-0003, ADR-0004, ADR-0005, ADR-0006

## Context

The economics of systems-code production are changing. Increasingly, C code is not typed line-by-line by a human developer. A human describes intent, constraints and architecture; an LLM or coding agent generates or edits substantial amounts of implementation code, runs tools, consumes diagnostics and iterates.

That change removes one of the historical objections to adding ownership metadata to C: annotation burden.

For a human-only workflow, adding explicit ownership, borrow, move and API-lifetime metadata can feel like extra syntax and migration cost. For an LLM-driven workflow, emitting and maintaining that metadata is cheap. The code generator can produce C& intent at the same time it produces C and can revise the implementation repeatedly in response to deterministic checker output.

This changes the most useful positioning of C&.

C& is not merely a migration aid for existing human-written C. It is also a **verification layer for machine-generated C**.

The design model becomes:

```text
human intent / requirements
          |
          v
   LLM / coding agent
    (synthesis engine)
          |
          v
 C + C& ownership intent
          |
          v
       cand check
    (verification engine)
          |
      +---+---+
      |       |
    reject   prove
      |       |
      v       v
structured   ordinary C build
obligations  + tests/sanitizers
      |
      +------> LLM repairs and retries
```

The LLM is not part of the trusted computing base. It is an untrusted synthesis engine whose output must satisfy C& and the ordinary project gates.

## Decision

C& SHALL treat LLM/coding-agent authorship as a first-class and core usage model.

The project SHALL optimize its interfaces for both humans and machines, with deterministic machine consumption considered mandatory rather than optional.

The canonical principle is:

> **LLMs synthesize. C& verifies.**

C& SHALL NOT require trust in the model that generated the source, annotations, proposed fixes or candidate contracts.

A safety claim comes from the C& verifier and its trusted inputs, never from an LLM assertion that the generated code is safe.

## LLM-first generation profile

C& SHOULD provide a generation-oriented strict profile for new machine-authored code.

In that profile:

- generated code is checked at the requested safety level by default;
- ownership-affecting APIs should be annotated or contract-resolved when generated;
- `CAND_OWN`, borrow, move and consume intent can be emitted directly during synthesis;
- unknown ownership effects fail closed;
- unsupported constructs are explicit;
- unsafe boundaries are exceptional and visible;
- the generator receives machine-readable diagnostics and proof obligations;
- the generator iterates until the verifier accepts the requested scope or reports a non-repairable/unsupported condition.

This is different from legacy migration, where C& may begin in observe mode and gradually classify an existing codebase.

## The verifier loop

A coding agent SHOULD be able to execute this loop without scraping human prose:

```text
1. generate/change C
2. run cand check --strict --format json
3. parse stable diagnostic objects
4. repair implementation/ownership intent
5. rerun cand check
6. run ordinary compiler/tests/sanitizers
7. obtain C& evidence report
8. present unsafe/unsupported/policy changes for human review
```

C& therefore requires stable diagnostic IDs, deterministic structured output, ownership traces, contract provenance, unsupported-state reporting and explicit repair classifications.

## Human role changes

In an LLM-first workflow, humans SHOULD spend less time maintaining routine ownership annotations and more time reviewing high-consequence trust boundaries.

The highest-value human review targets become:

- new or widened `unsafe` boundaries;
- trusted external API contracts;
- suppressions/baselines;
- safety-level reductions;
- unsupported constructs;
- ownership models for opaque external components;
- semantic repairs that alter program behavior;
- architecture and requirements.

C& SHOULD make those review surfaces easy to isolate from ordinary generated-code churn.

## Ownership metadata is intent, not proof

LLM-generated source annotations are useful because they communicate intended semantics, but an annotation alone is not proof.

For example:

```c
Buffer *p CAND_OWN = buffer_new();
```

states intended ownership. C& must still verify the subsequent control/data flow against that ownership model.

An LLM cannot make invalid code safe merely by adding `CAND_OWN`, `CAND_MOVE`, `CAND_BORROW`, or similar metadata.

External contracts are even more sensitive. An LLM MAY generate candidate contracts, but ADR-0006 applies: heuristic/AI-generated facts are not trusted proof inputs until they reach an approved trust class through review or sound derivation.

## Machine-oriented diagnostics

Every enforcement diagnostic SHOULD expose enough structured information for an agent to repair the code without guessing.

At minimum, where applicable:

- stable diagnostic ID;
- primary source range;
- safety level;
- abstract object/storage identity;
- owner origin;
- borrow origin;
- ownership state before and after the failing operation;
- relevant call/path trace;
- violated rule identifier;
- repair classification;
- candidate safe transformations when available;
- whether the problem can be solved only by changing a contract/unsafe boundary/policy.

Human prose remains important, but machine-readable semantics are authoritative for agent workflows.

## Proof artifact

A successful strict run SHOULD produce a machine-readable evidence artifact binding at least:

- source revision/content identity;
- C& version;
- requested safety level;
- checked scope;
- compiler-analysis profile;
- contract bundle digests and trust class;
- number and identity of unsafe boundaries;
- unsupported scope;
- suppressions/baselines;
- diagnostics outcome;
- proof/conformance suite version where applicable.

An LLM-generated statement such as “all memory issues are fixed” has no standing. A C& evidence artifact can support a scoped claim.

## Generation ergonomics

Because an LLM can cheaply maintain explicit semantics, C& SHOULD prefer machine-checkable explicitness over syntax-minimization when the two conflict.

This does **not** mean adding unnecessary noise. It means C& does not need to optimize exclusively for the minimum number of characters a human must type.

For new generated code, explicit ownership intent may be desirable even when inference could reconstruct it, because it improves review, diagnostics and regeneration stability.

Legacy human-written code remains supported through inference and incremental adoption.

## Agent-neutral design

C& SHALL NOT depend on a specific model vendor, agent framework, IDE, prompt format or proprietary protocol.

The integration boundary is ordinary process/file/JSON tooling:

```text
agent
  |
  +-- edits source/config
  +-- invokes cand
  +-- consumes deterministic JSON/SARIF/evidence
  +-- invokes ordinary build/test tools
```

Any capable coding agent should be able to use C&.

## Consequences

### Positive

- ownership annotation burden becomes much less important for new code;
- C can retain its ABI/ecosystem while gaining a strong verifier loop;
- LLMs can repair diagnostics rapidly and repeatedly;
- explicit ownership intent improves machine reasoning and human review;
- C& can become a natural gate for generated low-level code;
- the verifier is independent of model quality/vendor;
- safety claims do not depend on trusting probabilistic generation.

### Negative

- structured diagnostics and evidence become core product work, not secondary tooling;
- C& must defend against agents taking shortcuts to make checks pass;
- generated contracts/unsafe boundaries require careful trust policy;
- deterministic behavior matters more because agents will retry at high frequency;
- performance requirements become stricter because analysis may run many times per coding task;
- incremental/cache correctness becomes security-relevant.

## Invariants

1. The LLM/agent is never part of the trusted proof base.
2. Generated annotations express intent; they do not independently establish safety.
3. Candidate AI-generated contracts are untrusted until promoted through an approved trust path.
4. Machine-readable diagnostics are a first-class API.
5. A successful C& claim is bound to exact source/config/contracts/tool versions.
6. Agent integrations remain vendor/model neutral.
7. LLM-first generation does not weaken compatibility with human-written legacy C.
8. Strict generated-code profiles fail closed on unknown ownership semantics.
9. Proof-weakening changes must be separately visible and policy-controlled.
10. C& may help an agent repair code, but the verifier—not the agent—decides pass/fail.

## Acceptance criteria

ADR-0008 is implemented when:

1. an agent can consume deterministic JSON diagnostics without parsing human text;
2. diagnostics include sufficient ownership-state information for iterative repair on the mandatory corpus;
3. a generated-code strict profile exists;
4. a complete generate -> check -> repair -> check loop is demonstrated automatically;
5. the final successful run emits a versioned evidence artifact;
6. agent-generated candidate contracts cannot silently become trusted proof inputs;
7. unsafe/suppression/policy weakening is surfaced separately from ordinary source repair;
8. at least one model-neutral integration example demonstrates the full loop.
