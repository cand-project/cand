# ADR-0004: Annotation Non-Interference and Production-Build Equivalence

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** annotation semantics, production builds, compatibility evidence

## Context

C& adds ownership metadata to ordinary C. If that metadata changes runtime behavior, object layout, calling convention, optimization semantics, or generated machine code in production, C& stops being a pure analysis layer and becomes a language/runtime transformation system.

That would undermine one of the core project claims: C& should reveal and reject ownership problems in existing C, not make them disappear by changing the compiled program behind the developer's back.

This distinction is especially important for proof examples. An unsafe program with C& annotations must remain the same unsafe C program when the C& checker is bypassed. Otherwise a demonstration that “C& fixes use-after-free” could simply be caused by hidden instrumentation or rewriting rather than ownership enforcement.

## Decision

C& SHALL enforce an **annotation non-interference guarantee** for production builds.

When the C& analysis stage is not active, C& source annotations SHALL be code-generation-neutral metadata/no-ops, and expression markers such as `CAND_MOVE(x)` SHALL preserve the ordinary C expression semantics defined for their supported operand class.

At C&1, adding/removing C& metadata alone SHALL NOT intentionally change:

- runtime control flow;
- memory allocation/deallocation behavior;
- data layout;
- symbol names/linkage;
- calling convention;
- C ABI;
- thread synchronization;
- ownership at runtime;
- executable error handling;
- destructor/free ordering.

The production compiler remains responsible for compiling the ordinary C program.

## Analysis profile vs production profile

C& MAY expose richer compiler attributes only during the analysis profile:

```text
analysis invocation
  -DCAND_ANALYSIS=1
  -> __attribute__((annotate("cand:...")))
  -> AST metadata visible to checker

production invocation
  CAND_ANALYSIS absent
  -> annotations expand to no-op metadata
  -> CAND_MOVE(x) expands to ordinary expression form
  -> normal compiler/codegen
```

C& CI and documentation SHALL discourage enabling `CAND_ANALYSIS` in a production code-generation invocation. Even if a frontend's `annotate` attribute is currently codegen-neutral, C& does not require production builds to depend on that implementation detail.

## Supported expression-marker semantics

`CAND_MOVE(x)` is an analysis marker, not a runtime move operation.

Initially it SHALL be supported only for expression forms whose identity/ownership transfer can be modeled without changing evaluation behavior, primarily named pointer/object lvalues.

Example:

```c
Packet *p CAND_OWN = packet_new();
packet_send(CAND_MOVE(p));
```

In a production compile, this remains equivalent to:

```c
packet_send(p);
```

The checker changes the **static ownership state** after the call because the trusted API contract says the argument is consumed. The emitted C program is not rewritten to null the pointer, copy the object, or introduce runtime guards.

C& SHOULD reject or diagnose unsupported marker operands that could create ambiguity about evaluation count, side effects, or identity.

Examples that SHOULD be disallowed initially:

```c
CAND_MOVE(p++)
CAND_MOVE(get_pointer())
CAND_MOVE(array[i++])
```

unless a later SPEC defines exact safe semantics.

## Why an unsafe annotated program must still fail without enforcement

Consider:

```c
int *p CAND_OWN = malloc(sizeof *p);
free(p);
return *p;
```

Without `cand check`, the production compiler sees effectively ordinary C and the use-after-free remains present.

This is intentional.

The proof is:

```text
same source + C& metadata
       |
       +--> ordinary compiler only --> unsafe behavior still exists
       |
       +--> cand check first --------> rejected before production build
```

C& therefore prevents the defect by **enforcement**, not by hidden mitigation.

## Production-equivalence evidence

The repository SHALL maintain compatibility/equivalence tests at multiple strengths.

### Level A — preprocessing contract

For normal production compilation:

- C& annotation tokens must disappear or reduce to neutral constructs;
- analysis-only `cand:` annotation strings must not appear in the production preprocessed representation;
- `CAND_MOVE(x)` must not introduce multiple evaluation.

### Level B — ABI/layout contract

Fixtures SHALL verify where applicable:

- `sizeof` and `_Alignof` unchanged;
- struct/union field offsets unchanged;
- function pointer types/calling convention unchanged;
- exported symbol names unchanged.

### Level C — code-generation comparison

Representative fixtures SHOULD compare normalized optimized IR/assembly/object code for:

1. annotated source compiled in production profile;
2. equivalent source with C& metadata removed.

Differences caused only by source locations/debug metadata MAY be normalized away. Any meaningful code-generation difference caused by C& production annotations SHALL be investigated and treated as a compatibility regression unless explicitly approved by a future safety-level ADR.

Exact binary hash equality is not a universal requirement because compilers may embed source paths, build IDs, debug data, or nondeterministic metadata.

## Debug/runtime instrumentation

C& MAY later provide an optional debug validation mode that inserts runtime checks or poisoning.

If introduced, it MUST be:

- opt-in;
- clearly named as instrumentation;
- outside the production non-interference claim;
- separately benchmarked;
- not required for the C&1 static guarantee.

## Fix-it interaction

A Class A `cand fix --safe` edit defined by ADR-0003 must preserve production semantics. Most such edits are metadata changes.

Class B semantic repairs intentionally change source behavior and therefore fall outside annotation non-interference. They require explicit approval and are tested as new program revisions.

## Alternatives considered

### Null pointers automatically after `CAND_MOVE`

**Rejected.** This would change runtime behavior and could mask use-after-move bugs rather than prove their absence.

### Replace `free()` with checked runtime wrappers

**Rejected as the default C&1 model.** Useful as optional debug instrumentation, but not as the core ownership guarantee.

### Emit transformed C for production

**Rejected.** It violates original-source authority and complicates ABI/toolchain trust.

## Consequences

### Positive

- C& remains genuinely incremental tooling for C rather than a hidden dialect/runtime;
- A/B demonstrations of safety are credible;
- existing performance/codegen characteristics remain under the normal compiler's control;
- legacy builds can carry C& metadata before enforcement becomes mandatory;
- removing the checker does not silently substitute a different runtime model.

### Negative

- C& cannot make unchecked binaries safe by itself;
- developers may misunderstand annotations as runtime guards unless documentation is clear;
- `CAND_MOVE` operand forms must initially be conservative;
- equivalence testing across toolchains requires normalization and maintenance.

## Invariants

1. **Metadata is not mitigation.** Annotations do not repair runtime bugs by themselves.
2. **Production C remains ordinary C.**
3. **No C&1 ABI/layout change.**
4. **No implicit pointer nulling or runtime ownership object.**
5. **No multiple evaluation introduced by analysis markers.**
6. **Analysis-only compiler attributes stay out of the required production path.**
7. **Optional instrumentation is explicitly separate.**

## Acceptance criteria

ADR-0004 is implemented when CI can demonstrate that:

1. production preprocessing removes analysis-only C& annotations;
2. representative annotated/unannotated fixtures preserve ABI/layout;
3. `CAND_MOVE` supported operands evaluate exactly once and compile as ordinary C;
4. normalized production code generation is equivalent for representative metadata-only examples;
5. the same annotated unsafe use-after-free/double-free fixtures still trigger the independent runtime oracle when `cand check` is bypassed;
6. enabling C& enforcement rejects those fixtures before a production build is accepted.
