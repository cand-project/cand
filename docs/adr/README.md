# C& Architecture Decision Records

C& uses ADRs for repository-wide design decisions that constrain implementation and safety claims.

## Current decisions

| ADR | Decision | Status |
|---|---|---|
| [ADR-0001](ADR-0001-pipeline-safety-layer.md) | C& is a pipeline safety layer, not a C compiler | Proposed |
| [ADR-0002](ADR-0002-differential-safety-evidence.md) | Safety claims require differential ordinary-C/C& evidence | Proposed |
| [ADR-0003](ADR-0003-diagnostics-and-fixit-policy.md) | Diagnostics are read-only by default; only semantics-preserving fix-its are automatic | Proposed |
| [ADR-0004](ADR-0004-annotation-noninterference.md) | C& annotations do not alter production runtime semantics or ABI | Proposed |
| [ADR-0005](ADR-0005-enforcement-modes-and-claim-lifecycle.md) | Observation, enforcement, and release evidence are separate modes/claims | Proposed |
| [ADR-0006](ADR-0006-ownership-inference-and-trust.md) | Ownership facts have an explicit trust hierarchy; heuristic/AI inference is not proof | Proposed |
| [ADR-0007](ADR-0007-source-annotation-encoding.md) | Common ownership effects use one canonical C-compatible annotation encoding | Proposed |
| [ADR-0008](ADR-0008-llm-first-synthesis-and-verification.md) | LLMs/coding agents are first-class synthesis engines; C& is the deterministic verifier | Proposed |
| [ADR-0009](ADR-0009-agent-proof-policy.md) | Agents may repair implementation but must not silently weaken proof policy | Proposed |
| [ADR-0010](ADR-0010-trustworthy-pass.md) | Trustworthy PASS: no unresolved ownership operation may be silently accepted | Proposed |
| [ADR-0011](ADR-0011-cfg-flow-sensitive-ownership.md) | Ownership state is attached to CFG program points with a finite lattice and deterministic joins | Proposed |
| [ADR-0012](ADR-0012-storage-identity-and-alias-model.md) | Object lifetime is separate from pointer storage and alias relationships | Accepted (P0.3) |
| [ADR-0013](ADR-0013-interprocedural-ownership-summaries.md) | Calls require verified summaries or explicitly trusted contracts | Accepted (P0.4) |
| [ADR-0014](ADR-0014-agent-verification-evidence.md) | Agent output is accepted only under runner-authorized policy and replayable input-bound evidence | Accepted (P0.5 slice) |
| [ADR-0015](ADR-0015-trusted-agent-attestation.md) | Authoritative agent verification uses a protected base-built verifier, not candidate-controlled CI | Proposed (P0.5.1) |

## Design chain

Together the ADRs define the project in one sentence:

> C& analyzes ordinary C before the existing compiler, treats LLMs and humans as untrusted code producers, derives or consumes reviewed ownership contracts, rejects invalid ownership/lifetime states in checked scope, gives machines structured proof obligations and humans focused trust-boundary review, prevents agents from silently weakening the safety claim, and proves its claims with paired unsafe/safe evidence.

The implementation specifications under `../spec/` define the lower-level semantic contracts, including [SPEC-0004 — Machine-Agent Verification Protocol](../spec/SPEC-0004-machine-agent-protocol.md).
