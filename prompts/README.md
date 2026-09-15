# C& Review and Engineering Prompts

These prompts are reusable instructions for independent LLM/coding-agent review of C&.

They are intentionally **model-neutral** and adversarial. A reviewer may use Kimi, Claude, Codex, GPT, Gemini, a local model, or another capable coding agent.

## Authority

Prompt files are **not normative project specifications**.

If a prompt conflicts with an accepted ADR, normative SPEC, machine-readable contract, executable test, or the implementation's documented safety boundary, the project authority wins.

Prompts must never be used to expand a safety claim.

## Available prompts

- [`01-INDEPENDENT-REPOSITORY-REVIEW.md`](01-INDEPENDENT-REPOSITORY-REVIEW.md) — broad architecture, implementation, documentation and evidence review.
- [`02-SOUNDNESS-RED-TEAM.md`](02-SOUNDNESS-RED-TEAM.md) — adversarial search for false PASS, false FAIL, fail-open semantics and proof-policy bypasses.
- [`03-LLM-GENERATED-C-EVALUATION.md`](03-LLM-GENERATED-C-EVALUATION.md) — test the central `LLM -> C -> C& -> repair -> verify` thesis on natural generated C.
- [`04-IMPLEMENTATION-ITERATION.md`](04-IMPLEMENTATION-ITERATION.md) — disciplined implementation workflow for future P0/P1/P2 phases with review-before-merge requirements.

## Required reviewer behavior

All prompts assume these rules:

1. Do not trust README claims without checking code and tests.
2. Do not reinterpret `INCOMPLETE` as failure of the idea; distinguish coverage from unsoundness.
3. Treat false PASS as the highest-priority semantic defect.
4. Do not weaken policy, scope, contracts or expected tests to make results green.
5. Report exact commit SHA and toolchain versions.
6. Preserve minimal reproducers for every semantic defect found.
7. Never describe P0/P0.x behavior as a C&1 soundness proof.
8. For a potentially security-sensitive false safety claim, follow `SECURITY.md` rather than publishing exploit details prematurely.

## Suggested use

For public review, start with prompt 01. Compiler/static-analysis/security reviewers should then run prompt 02. Reviewers specifically interested in AI-generated systems software should also run prompt 03.

Prompt 04 is primarily for maintainers implementing the next architecture phase.
