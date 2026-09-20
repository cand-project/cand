# ADR-0022 — C&1/v1 Final Qualification Boundary

Status: Release candidate; technical qualification is complete on the precursor head, and acceptance remains pending final exact-head review and protected merge.

## Decision

C&1/v1 is a narrow, generated-profile, evidence-bound temporal ownership claim.
The implementation may claim only the semantics in SPEC-0010 and only on the
qualified toolchain profile. Unsupported behavior is an incomplete/tool-policy
result, never a successful proof.

The single C&1 PASS authority is `canEmitCand1Pass` in `src/cand.cpp`. It is
intentionally downstream of semantic diagnostics, unsupported accounting,
frontend/contract errors, exact policy bindings, trusted-contract checks,
toolchain validation, and evidence identity. Ordinary semantic PASS is not a
release claim.

Cross-TU analysis without a reviewed contract is outside v1. The same boundary
applies to compiler extensions, plugins, response files, unknown external
effects, unsafe regions, and unmodeled pointer transport.

## Consequences

- The checked scope stays explicit and small.
- Conservative INCOMPLETE is acceptable; false PASS is a blocker.
- Evidence replay and two clean builds are release requirements.
- A confirmed false PASS suspends and revokes the claim until complete
  requalification.
- README and `docs/SAFETY_CLAIMS.md` carry the narrow candidate wording; protected main remains non-claiming until the final gate.

## Rejected alternatives

- Treating green tests or sanitizer cleanliness as proof.
- Expanding syntax support to avoid INCOMPLETE.
- Letting GitHub approval or historical attestation substitute for semantic or
  evidence qualification.
- Claiming general C memory safety, spatial safety, null safety, integer safety,
  provenance safety, or concurrency safety.
