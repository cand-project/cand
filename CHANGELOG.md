# Changelog

All notable project changes are recorded here.

## Unreleased

### LLM-first architecture

- made **“LLMs synthesize. C& verifies.”** a core project thesis rather than an optional integration;
- added ADR-0008 defining coding agents as first-class untrusted synthesis engines;
- added ADR-0009 defining proof-policy protection against agent reward-hacking/shortcutting;
- added SPEC-0004 defining the deterministic machine-agent verification protocol;
- added `contracts/agent-policy.yaml` with a strict generated-code safety budget;
- made machine-readable ownership-state diagnostics, repair classes, policy deltas and evidence artifacts first-class design requirements;
- added generated-code strict mode and autonomous repair-loop gates to the roadmap;
- clarified that LLM-generated annotations are intent, candidate contracts are untrusted, and model-generated safety claims have no proof status;
- shifted the intended human review surface toward unsafe boundaries, trusted contracts, suppressions, unsupported code and semantic policy changes.

## 0.1.0 — 2026-09-14

First public architecture and compatibility baseline for C&.

### Included

- project identity: **C& — C with Ownership**;
- ADR-0001 defining C& as an analysis/enforcement stage, not a compiler fork;
- normative ownership, borrowing, pipeline, and external-contract specifications;
- portable C annotation header under `include/cand/cand.h`;
- machine-readable safety levels, diagnostics, libc ownership contracts, and contract schema;
- GCC and Clang compatibility fixtures;
- CI validation for contracts and ordinary-C compatibility;
- project security, contribution, and governance files;
- corrected canonical logo and project-purpose/build-pipeline diagrams.

### Safety status

Version 0.1.0 is a design and compatibility baseline. It does **not** claim that C&1 temporal ownership safety is implemented or proven sound. Such a claim requires the acceptance and evidence gates defined by the SPECs and roadmap.
