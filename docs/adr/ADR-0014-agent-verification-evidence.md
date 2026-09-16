# ADR-0014: Agent Verification Evidence and Policy Authority

- **Status:** Accepted (P0.5 slice)
- **Date:** 2026-09-16
- **Decision owners:** C& maintainers
- **Scope:** generated-code verification, proof-policy deltas, evidence
- **Depends on:** ADR-0008, ADR-0009, ADR-0010, ADR-0013

## Context

An agent that controls both generated source and its effective checker
configuration could otherwise turn a failed check into an apparent success by
changing scope, compiler inputs, trusted contracts, or policy. C& must separate
implementation repair from changes to the authority and inputs used to judge
that repair.

## Decision

P0.5 implements a deliberately narrow `generated` profile for
`p0-temporal-lifecycle` and C11. The versioned `cand.policy/v1` configuration
requires an explicit source scope, zero unsafe/suppression/unsupported
weakening budgets, and an exact list of additional frontend arguments. The
agent CLI cannot select a different base policy or substitute a different
policy path. The configured policy is compared with `origin/main`; weakening
fails, while trust/profile/policy changes that require a person are marked
`REVIEW_REQUIRED`.

The repository's effective policy and a candidate contract do not grant their
own authority. Trusted contracts must match an explicit path, SHA-256 and
trust-class pin in the policy inherited from the base. Candidate trust class
cannot support analysis. The runner supplies `CAND_TRUSTED_BASE_SHA` from its
protected context; it must match the resolved `origin/main` commit. A local
agent can set environment variables or fabricate local output, so only the
protected CI runner's invocation and result are authoritative.

`cand.evidence/v1` records the source and project-local include digests, exact
running `cand` binary digest, Clang/LLVM identity and frontend arguments,
trusted base SHA, effective policy, checked scope, used contracts, analysis
counts, and separate semantic/policy result. Evidence is deterministic for
identical inputs. `cand evidence verify` checks hashes and replays analysis
with the current binary before accepting the recorded payload.

The embedded SHA-256 is unkeyed. It detects modifications that are not
recalculated but is not a signature or authenticity mechanism. CI must rerun
the trusted verifier; this phase does not provide cryptographic signing.

## Consequences

- source repair can progress while policy weakening remains visible and
  blocked from verified success;
- policy, source, contract, frontend, verifier-binary, and base changes make
  old evidence stale or require review;
- exact compiler arguments and explicit checked scope reduce portability and
  convenience, intentionally favoring fail-closed behavior;
- semantics outside the existing analyzer remain FAIL or INCOMPLETE, never
  assumed safe;
- this does not claim general C memory safety or full C&1.
