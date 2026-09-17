# ADR-0015: Protected Trusted Agent Attestation

- **Status:** Proposed (P0.5.1 hardening)
- **Date:** 2026-09-16
- **Decision owners:** C& maintainers
- **Scope:** authoritative CI verification for LLM/agent-authored changes
- **Depends on:** ADR-0008, ADR-0009, ADR-0010, ADR-0014

## Context

P0.5 implemented strict agent policy, deterministic evidence and replay, but a
local or candidate-built invocation is not itself proof authority. An agent can
control the candidate source tree, including build scripts and future verifier
changes. Running the candidate verifier and candidate workflow is useful
implementation CI, but it cannot be the independent authority that decides
whether that same candidate weakened its own verification boundary.

ADR-0014 already states this requirement: only a protected runner invocation is
authoritative. P0.5.1 makes that execution boundary concrete.

## Decision

C& will maintain two distinct CI roles.

### Candidate implementation CI

The ordinary `CI` workflow checks out the candidate revision, builds the
candidate C& implementation, and executes its complete regression suite. It
answers:

> Does the proposed verifier implementation build and pass its tests?

It does **not** independently attest that the candidate could not weaken those
same tests or verifier semantics.

### Trusted agent attestation

`Trusted Agent Attestation` runs using `pull_request_target`, so workflow logic
comes from the protected PR base. It:

1. checks out the PR base separately as trusted verifier source;
2. builds `cand` only from that base;
3. checks out PR head in a separate directory and treats it only as input;
4. pins the candidate checkout's `origin/main` ref to the event's immutable
   `pull_request.base.sha`;
5. supplies `CAND_TRUSTED_BASE_SHA` from trusted event context;
6. derives checked scope/frontend arguments from candidate policy, but lets the
   base-built verifier compare that policy with the pinned base policy;
7. rejects source/policy paths that escape the candidate workspace and rejects
   candidate-selected external include/config paths;
8. classifies verifier-authority paths from the protected
   `.github/trusted/verifier-surface.json` manifest, including `src/`,
   `include/cand/`, contracts, tests, build scripts, workflows and policy;
9. requires a fresh exact-head approval from a trusted reviewer when verifier
   authority changes or C& returns `REVIEW_REQUIRED`;
10. requires semantic `PASS` even when a review is present; a review cannot
    override `FAIL`, `INCOMPLETE` or `fail-policy`;
11. replays emitted evidence with the same base-built verifier before the job
    may succeed;
12. never executes candidate CMake, shell scripts, binaries or test harnesses
    before the trusted decision.

## Review authority

The protected workflow owns the trusted-reviewer list. Candidate changes to
that workflow are themselves verifier-authority changes and are evaluated using
the prior protected version. The list must remain aligned with repository
CODEOWNER/governance authority.

This workflow-level approval check complements, rather than replaces, GitHub
branch protection. `main` must require both normal implementation CI and the
trusted attestation check before merge.

## External compilation dependencies

P0.5 evidence binds project-local includes. P0.5.1 additionally prevents the
candidate from explicitly selecting include/config paths outside its checkout.
Default system/toolchain headers remain part of the supported toolchain trust
boundary until issue #12 defines a reproducible toolchain/sysroot strategy.
Arbitrary external non-system dependencies must not silently become trusted
inputs.

Evidence separates the base-built verifier source commit from the analyzed
candidate source commit, and records the trusted base revision, policy revision
and digest, frontend/toolchain identity, source/include manifest, contract
identities, checked scope, semantic result, and policy result. The verifier
source commit is embedded at trusted build time; it is never inferred from the
candidate worktree.

## Security properties

A candidate must not be able to obtain an authoritative green attestation by:

- editing the candidate verifier;
- editing candidate CI/build scripts;
- changing `origin/main` or supplying another base SHA;
- reducing checked scope;
- changing frontend inputs;
- adding unsafe/suppressions;
- substituting or self-trusting a contract;
- modifying emitted evidence;
- receiving human approval for a semantic FAIL or INCOMPLETE result.

Verifier-surface changes can proceed only through a human-reviewed transition;
they are not treated as ordinary source repairs.

## Consequences

- candidate CI and trusted attestation intentionally duplicate some work but
  answer different trust questions;
- verifier changes normally require exact-head trusted review;
- the attestation workflow is itself security-sensitive and must be protected
  by branch rules;
- fork PRs receive no write permissions or repository secrets from this job;
- evidence remains reproducibility/integrity evidence, not a cryptographic
  signature or general C memory-safety proof;
- no full C&1 claim follows from this ADR.

## Remaining work

- enforce repository ruleset/branch protection (issue #15);
- complete toolchain/sysroot reproducibility and support matrix (issue #12);
- close evidence over explicitly supported external dependencies;
- enforce the attestation status through repository rules (issue #15);
- define a reproducible system-header/sysroot identity (issue #12).
