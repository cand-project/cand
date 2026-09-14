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

## Design chain

Together the ADRs define the project in one sentence:

> C& analyzes ordinary C before the existing compiler, derives or consumes reviewed ownership contracts, rejects invalid ownership/lifetime states in checked scope, explains and suggests repairs without silently changing semantics, and proves its claims with paired unsafe/safe evidence.

The implementation specifications under `../spec/` define the lower-level semantic contracts.
