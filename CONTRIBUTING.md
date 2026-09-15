# Contributing to C&

C& is an evidence-first systems-safety project. Contributions are welcome, but changes that strengthen a safety claim must come with executable evidence and a clearly defined boundary.

## Before changing semantics

Read, in order:

1. [`README.md`](README.md)
2. [`REVIEWING.md`](REVIEWING.md)
3. [`docs/SAFETY_CLAIMS.md`](docs/SAFETY_CLAIMS.md)
4. [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md)
5. `docs/adr/ADR-0001-pipeline-safety-layer.md`
6. `docs/adr/ADR-0010-trustworthy-pass.md`
7. the ADR/SPEC documents relevant to your change
8. `docs/ROADMAP.md`

Architecture changes require an ADR. Normative safety-semantics changes require a SPEC change or a new SPEC.

The reusable files under `prompts/` are review/engineering aids, **not project authority**. If a prompt conflicts with ADR/SPEC/contracts/tests, the project authority wins.

## Non-negotiable invariants

- C& is not a production C compiler or code generator.
- C& does not require a Clang/GCC fork.
- Analysis annotations must not alter runtime behavior, ABI, calling convention, or data layout.
- Unsupported ownership-relevant constructs fail closed in strict checked scopes.
- A suppressed/baselined finding is not proof.
- Safety claims must name their C& level/profile and checked scope.
- Generated or AI-suggested external contracts are untrusted until reviewed and accepted through the defined trust path.
- An LLM/coding agent is a synthesis engine, not part of the trusted computing base.
- Proof-policy weakening is not an ordinary implementation repair.
- P0/P0.x feature completion must not be described as C&1 soundness.

## Security and soundness reports

C& is verifier/security tooling. A defect that makes unsafe code satisfy a published safety claim can be security-relevant.

Before publishing details of a suspected false safety claim, read [`SECURITY.md`](SECURITY.md). Public issues are appropriate for false FAILs, unsupported constructs, usability gaps, documentation/schema drift, and non-sensitive analyzer bugs.

## Pull requests

Keep PRs focused. Use the repository PR template fully.

For semantic changes, include:

- exact base and final HEAD SHA;
- relevant ADR/SPEC clause;
- what becomes newly supported;
- what remains `INCOMPLETE`;
- safe/positive fixture(s);
- violation/negative fixture(s);
- explicit unsupported fixture(s) where the model stops;
- differential/independent evidence where useful;
- a fresh post-implementation adversarial review;
- an honest list of remaining BLOCKER/HIGH/MEDIUM/LOW findings.

Do not upgrade README safety claims merely because a feature appears implemented. The corresponding acceptance/evidence gate must pass.

## Validation

Run the repository checks before opening or updating a semantic PR:

```bash
bash scripts/check.sh

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

Run phase-specific differential/adversarial suites as documented by the tests changed in your PR.

A green test suite is necessary but not sufficient for a verifier change. Reviewers should actively try to construct a false PASS after implementation.

## Review-until-clean discipline

When a review finds a semantic bug:

1. minimize the reproducer;
2. classify severity;
3. add a permanent regression fixture;
4. fix the root cause;
5. rerun the full suite;
6. perform another fresh adversarial pass.

Do not merge solely because the originally reported case was fixed. The final review pass should find no remaining BLOCKER/HIGH issue within the PR's stated claim.

## Commit and history quality

Use focused commits and descriptive messages. Do not hide semantic changes in formatting/refactor commits. Keep implementation reports and ADR/SPEC references synchronized with the exact final PR HEAD.

## Design discussions

Use GitHub issues for non-sensitive architecture proposals. Significant changes to the verifier trust boundary, result semantics, contracts, frontend assumptions, or safety levels should be discussed before implementation and captured in an ADR/SPEC when accepted.
