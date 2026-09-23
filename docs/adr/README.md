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
| [ADR-0015](ADR-0015-trusted-agent-attestation.md) | Historical protected-agent-attestation design; GitHub review is the active merge authority | Superseded |
| [ADR-0016](ADR-0016-unique-ownership-and-move-semantics.md) | Explicit moves transfer one ownership capability without changing ordinary C runtime semantics | Accepted (P1) |
| [ADR-0017](ADR-0017-p2-borrow-lifetime-verification.md) | Explicit borrow relationships derive their lifetime from an ObjectId parent and fail closed after invalidation | Proposed (P2) |
| [ADR-0018](ADR-0018-cand1-profile-and-heap-abstraction.md) | C&1 strict profile and bounded heap generations | Accepted (C&1-A) |
| [ADR-0019](ADR-0019-cand1-transport-boundary.md) | C&1 fail-closed pointer transport boundary | Accepted (C&1-B) |
| [ADR-0020](ADR-0020-cand1-differential-verification.md) | C&1 differential verification infrastructure | Accepted (C&1-C) |
| [ADR-0021](ADR-0021-cand1-toolchain-profile.md) | Narrow reproducible C&1 toolchain profile | Accepted (C&1-D) |
| [ADR-0022](ADR-0022-cand1-v1-final-qualification.md) | C&1/v1 is a narrow generated-profile, evidence-bound temporal ownership claim | Release candidate; final review pending |
| [ADR-0023](ADR-0023-cand1-parameter-lifetime-repair.md) | C&1 parameter-lifetime soundness repair | Proposed (#46 repair branch) |
| [ADR-0024](ADR-0024-parameter-identity-completion.md) | Parameter-identity completion: Unknown-capability pointer parameters are tracked live-at-entry objects with no authority | Accepted |
| [ADR-0025](ADR-0025-reassigned-parameter-origin-repair.md) | Reassigned-parameter borrow-origin repair: a parameter assigned anywhere in the body is not a sound return borrow origin (incident #62) | Accepted |
| [ADR-0026](ADR-0026-compound-origin-resolution-repair.md) | Compound-origin borrow-resolution repair: return origins resolve only from expressions unambiguously derived from a single pointer parameter (incident #64) | Accepted |
| [ADR-0027](ADR-0027-conditional-borrow-none-join.md) | Conditional borrow/none effect join: a parameter at most borrowed (or untouched) on every path is at most borrowed overall (milestone #61) | Accepted |
| [ADR-0028](ADR-0028-bounded-local-alias-destruction-attribution.md) | Bounded local-alias destruction attribution: consuming calls through a single-assignment declaration-init alias of one parameter attribute to that parameter (milestone #54) | Accepted |
| [ADR-0029](ADR-0029-declaration-annotation-review-manifest.md) | Declaration-annotation review manifest: body-less annotated declarations seed summaries only through a separately reviewed, policy-pinned manifest with exactly equal facts (milestone #39) | Accepted |

## Design chain

Together the ADRs define the project in one sentence:

> C& analyzes ordinary C before the existing compiler, treats LLMs and humans as untrusted code producers, derives or consumes reviewed ownership contracts, rejects invalid ownership/lifetime states in checked scope, gives machines structured proof obligations and humans focused trust-boundary review, prevents agents from silently weakening the safety claim, and proves its claims with paired unsafe/safe evidence.

The implementation specifications under `../spec/` define the lower-level semantic contracts, including [SPEC-0004 — Machine-Agent Verification Protocol](../spec/SPEC-0004-machine-agent-protocol.md) and [SPEC-0005 — P2 Borrow Relationships and Lifetime Verification](../spec/SPEC-0005-p2-borrow-lifetimes.md).
