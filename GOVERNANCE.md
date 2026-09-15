# C& Governance

C& is an independent open-source systems project developed in public.

The project is currently in an early maintainer-led phase. Governance is intentionally simple while the architecture and verifier trust model are still being established.

## Current model

- Maintainers are responsible for repository stewardship, releases, security response, and final merge decisions.
- Contributors are encouraged to challenge architecture and safety claims through issues, pull requests, reproducible experiments, and independent review.
- Safety-critical decisions are documented rather than relying on informal maintainer intent.

The current CODEOWNERS file identifies the maintainers responsible for review of project areas.

## Decision authority

Different changes use different authority mechanisms:

| Change | Required authority |
|---|---|
| implementation bug fix without semantic change | code review + tests |
| architecture/trust-boundary change | ADR |
| normative ownership/lifetime semantics | SPEC |
| machine contract/schema change | versioned contract/schema + review |
| published safety-claim change | ADR/SPEC + evidence gate + documentation update |
| security-sensitive soundness fix | security process + regression evidence |

Prompts, implementation reports, README prose, benchmark results, and LLM opinions do not override accepted ADR/SPEC/contracts.

## Safety decisions

C& is verifier/security tooling. Safety claims require a higher bar than ordinary feature claims.

A change that broadens `PASS`, reduces `INCOMPLETE`, adds a trusted input, changes checked scope, or changes proof policy must explain why the new behavior remains conservative and must include adversarial evidence appropriate to the change.

Known false PASS is a merge blocker in the claimed scope.

## Pull requests

Maintainers should not merge semantic changes solely because CI is green.

For verifier changes, review should include:

- normative authority (ADR/SPEC/contract);
- positive, negative and unsupported fixtures;
- schema/documentation consistency;
- post-implementation adversarial review;
- exact final-HEAD CI;
- explicit safety-claim boundary.

The pull request template captures the current minimum gate.

## Architecture proposals

Use a GitHub issue for significant design proposals before implementation when practical.

An accepted architecture change should be captured in an ADR containing:

- context/problem;
- decision;
- alternatives considered;
- security/soundness implications;
- compatibility implications;
- consequences and known limitations.

Normative language behavior belongs in a SPEC rather than only an ADR.

## Security disclosures

Potentially sensitive false-safety-claim findings follow `SECURITY.md`.

Security fixes should add minimized regression evidence after disclosure is safely handled.

## Releases

A release must not advertise a safety level beyond the evidence gate that has actually passed.

Release notes should identify:

- exact verifier version;
- supported toolchain/frontend range;
- implemented safety level/profile;
- important unsupported semantics;
- evidence/test status;
- security-relevant fixes since the previous release.

The planned C&1 guarantee has a separate tracking issue and must not be inferred from P0/P1/P2 feature completion.

## Adding maintainers

As the contributor base grows, maintainers may add reviewers/maintainers based on sustained technical contributions, sound review judgment, respect for the project's fail-closed safety discipline, and reliable participation.

The governance model should be revisited when multiple maintainers are actively responsible for independent project areas. Any material governance change should be proposed publicly.

## Project principle

> **C& should be easier to challenge than to overclaim.**

The project benefits when external reviewers can reproduce results, identify limitations, and see known gaps tracked openly.