# ADR-0022 — C&1/v1 Final Qualification Boundary

Status: Accepted; C&1/v1 qualified on protected main.

Reviewed qualification HEAD: `7a6f4b65fb6e7506d073f9c93aa615c0e6bf8860`
Merge/release commit: `3a2672b6b6a6742c8ac19c2894a698cdd1970b7a`

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
- README and `docs/SAFETY_CLAIMS.md` carry the narrow qualified wording; the
  public claim remains bounded by SPEC-0010 and the qualified profile.

## Rejected alternatives

- Treating green tests or sanitizer cleanliness as proof.
- Expanding syntax support to avoid INCOMPLETE.
- Letting GitHub approval or historical attestation substitute for semantic or
  evidence qualification.
- Claiming general C memory safety, spatial safety, null safety, integer safety,
  provenance safety, or concurrency safety.
