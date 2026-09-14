# Contributing to C&

C& is an evidence-first systems-safety project. Contributions are welcome, but changes that strengthen a safety claim must come with executable evidence and a clearly defined boundary.

## Before changing semantics

Read, in order:

1. `docs/adr/ADR-0001-pipeline-safety-layer.md`
2. `docs/spec/SPEC-0001-ownership-and-borrowing.md`
3. `docs/spec/SPEC-0002-analysis-pipeline-and-interoperability.md`
4. `docs/spec/SPEC-0003-contract-format.md`
5. `docs/ROADMAP.md`

Architecture changes require an ADR. Normative safety-semantics changes require a SPEC change or a new SPEC.

## Non-negotiable invariants

- C& is not a production C compiler or code generator.
- C& does not require a Clang/GCC fork.
- Analysis annotations must not alter runtime behavior, ABI, or data layout.
- Unsupported constructs fail closed in strict checked scopes.
- A suppressed/baselined finding is not proof.
- Safety claims must name their C& level and checked scope.
- Generated or AI-suggested external contracts are untrusted until reviewed and accepted.

## Pull requests

Keep PRs focused. Include tests/fixtures for semantic changes and explain which ADR/SPEC clause the change implements. Do not upgrade README safety claims merely because a feature appears implemented; the corresponding acceptance/evidence gate must pass.

Run the repository checks before opening a PR:

```bash
bash scripts/check.sh
```
