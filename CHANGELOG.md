# Changelog

All notable project changes are recorded here.

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
