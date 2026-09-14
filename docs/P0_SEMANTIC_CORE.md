# P0 Semantic Core

P0 is the first executable C& verification slice. It exists to prove the architecture and machine-repair loop without claiming that the full C&1 ownership model is implemented.

## What P0 proves

For the supported straight-line subset, `cand check` tracks objects returned by `malloc`/`calloc`, observes direct `free` operations, and enforces two temporal lifecycle rules:

- `CAND-T002` — use after object destruction;
- `CAND-T003` — repeated destruction of the same tracked object.

The implementation is an upstream-Clang LibTooling executable. It does not generate code or replace the final C compiler.

## Machine interface

Example:

```bash
cand check --format=json example.c -- -std=c11 -Iinclude
```

Results use the versioned `cand.check/v1` envelope and include stable diagnostic IDs, abstract object IDs, source locations, repair classes, and object-state traces suitable for a coding agent.

Exit status is part of the P0 contract:

| Exit | Result | Meaning |
|---:|---|---|
| `0` | `pass` | no P0 lifecycle violation or unsupported construct was encountered |
| `1` | `fail` | at least one enforced P0 lifecycle rule was violated |
| `2` | tool/input error | invocation, parsing, or tooling failed |
| `3` | `incomplete` | P0 encountered semantics it does not model safely |

An autonomous agent MUST NOT treat exit `3` as success.

## Fail-closed boundary

P0 intentionally does not model general aliasing, arbitrary branch/loop dataflow, interprocedural ownership, borrowing, callbacks, `realloc`, or external ownership contracts.

When P0 recognizes a construct that could invalidate its lifecycle reasoning, it reports `CAND-U001` and returns `incomplete` rather than silently claiming a pass. Initial examples include pointer alias creation, unknown calls receiving a tracked pointer, nontrivial `free` arguments, tracked-pointer returns, and nontrivial control flow.

This boundary is conservative by design. P0 is evidence for the pipeline and agent protocol, not a substitute for the later semantic phases in `docs/ROADMAP.md`.

## LLM-first loop

The intended first machine loop is:

```text
LLM generates C
      |
      v
cand check --format=json
      |
   +--+----------------+
   |                   |
 fail/incomplete      pass
   |                   |
   v                   v
structured           ordinary C build
obligation           + tests/sanitizers
   |
   v
LLM repairs or escalates
   |
   +------> cand check
```

The model is an untrusted synthesis client. C& is the deterministic verifier. An `incomplete` result requires a stronger implementation/model or an explicitly reviewed proof-boundary decision; the agent may not silently convert it into a safety claim.

## Current proof corpus

CI verifies that:

- ordinary unsafe use-after-free maps to `CAND-T002`;
- the same defect remains detectable when C& annotations are present but enforcement is bypassed;
- double-free maps to `CAND-T003`;
- known-correct paired fixtures pass;
- aliasing and unknown ownership-affecting calls fail closed as `incomplete`.

These tests demonstrate the P0 contract only. They do **not** establish complete C&1 temporal ownership soundness.
