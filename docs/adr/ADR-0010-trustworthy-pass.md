# ADR-0010: Trustworthy PASS — Completeness Before Acceptance

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** analyzer semantics, result classification, PASS-completeness invariant, regression/evidence policy
- **Depends on:** ADR-0001, ADR-0002, ADR-0005, ADR-0006

## Context

An analyzer that sometimes misses real defects is merely incomplete. An analyzer that reports **PASS for a program it did not actually model** is worse than useless: it converts an unknown into a false safety claim.

An independent adversarial evaluation of the P0 analyzer found three AddressSanitizer-confirmed heap use-after-free programs for which C& returned PASS:

1. an allocation stored in a **struct member** (`s.p = malloc(...)`);
2. an allocation stored in an **array element** (`items[0] = malloc(...)`);
3. an allocation returned through an **unmodelled wrapper function** (`int *p = make_value()`).

All three share one root cause: operations that affect heap ownership/lifetime were encountered, not understood, and then **silently ignored**, so the collector concluded "no findings, no unsupported constructs, therefore PASS".

That is a blocker-class defect for any verification-oriented project, and it is exactly the failure mode an LLM-first workflow amplifies: an agent told "make CI green" will happily generate code shaped like any silent hole the verifier has.

## Decision

C& SHALL adopt the following foundational result semantics:

```text
PASS       = no known violation
             AND no unresolved ownership/lifetime operation
             within the P0 checked scope

FAIL       = C& found a known ownership/lifetime violation

INCOMPLETE = C& encountered ownership/lifetime semantics it cannot
             currently model (unknown call, alias, unsupported storage,
             unknown pointer-return ownership, unsupported control flow, ...)

ERROR      = tool/frontend/input failure (never a C& verdict)
```

Exit codes are fixed and stable:

```text
0 = PASS
1 = FAIL
2 = tool/input error
3 = INCOMPLETE
```

The analyzer SHALL classify every heap-relevant operation it encounters into exactly one of:

```text
SUPPORTED
KNOWN SAFE
KNOWN VIOLATION
UNSUPPORTED / INCOMPLETE
```

There SHALL be no fifth category of "unknown but still PASS".

Concretely, in P0.1:

- `free(expr)` on a tracked object performs the ownership transition;
- `free(NULL)` (a null pointer constant) is KNOWN SAFE per ISO C and allowed;
- `free(var)` on an untracked pointer variable is INCOMPLETE
  (`free-untracked-pointer`) — this includes function parameters, for which
  no ownership context exists yet;
- `free(expr)` on any other expression (member access, subscript, call
  result, ...) is INCOMPLETE (`free-untracked-expression`);
- allocation assigned to storage C& cannot model (struct member, array
  element, pointee store) is INCOMPLETE (`allocation-to-untracked-storage`);
- a pointer initialized or assigned from a non-allocator pointer-returning
  call with no trusted contract or interprocedural summary is INCOMPLETE
  (`unknown-pointer-return-ownership`);
- allocation results escaping as arguments into unknown calls, and
  dereferences of unknown pointer-returning call results, are INCOMPLETE;
- returning a pointer to automatic storage (`&local`, array decay of a
  local; `static`/globals/parameters excluded) is INCOMPLETE
  (`stack-pointer-return`) — a lifetime bug that involves no tracked heap
  object and would otherwise pass silently;
- inline assembly (`AsmStmt`) and GNU statement expressions (`StmtExpr`,
  including at expression positions) are INCOMPLETE (`inline-asm`,
  `statement-expression`) because the linear analyzer cannot model them;
- ownership-affecting operations in conditionally evaluated positions
  (a branch of `?:` or the short-circuited RHS of `&&`/`||`) are INCOMPLETE
  (`conditional-expression`, `short-circuit-expression`) and must not drive
  linear state transitions — otherwise a single `free` on one branch would
  be misreported as a double destruction;
- file-scope pointer initializers are classified (in ISO C they can only be
  constant expressions, which are known safe; the frontend rejects
  non-constant initializers with exit 2).

When known violations and unsupported obligations coexist, the result is
FAIL (exit 1) and the JSON output still lists the unsupported obligations.

## Rationale

**False INCOMPLETE is temporarily acceptable. False PASS is not.**

INCOMPLETE tells the agent exactly where the verifier's model ends and what
to refactor, annotate, or cover with a reviewed contract. PASS must mean:
every ownership-affecting operation in the checked scope was either
understood by the verifier or covered by a trusted contract. A PASS that
only means "no CAND-T002/T003 happened to be emitted" is forbidden.

This deliberately inverts the usual static-analyzer posture. Most analyzers
treat unmodelled code as "no finding". C& treats unmodelled
ownership-relevant code as an unresolved proof obligation, because its
consumer is an autonomous synthesis loop that will exploit any silent gap.

## Consequences

### Positive

- PASS becomes a meaningful, defensible claim within the declared P0 scope.
- The ASan differential corpus can enforce the invariant in CI: any fixture
  with an ASan-confirmed heap violation that C& passes fails the build.
- Agents cannot route around the verifier by shaping code into silent holes.
- The set of `CAND-U001` kinds forms an honest, machine-readable map of
  the analyzer's current semantic frontier.

### Negative

- Many ordinary, correct C programs return INCOMPLETE until later phases
  (flow sensitivity, aliases, interprocedural summaries, member/array
  tracking) close the gaps.
- There is pressure to "make programs pass" by reshaping them into the
  provable subset. This ADR explicitly rejects that: **the verifier must
  evolve toward real C; application code must not be contorted into a
  C& dialect.** INCOMPLETE on idiomatic C is a roadmap input, not a
  defect to be hidden.
- Exit code 2 for frontend/input errors is a compatibility break with any
  consumer that assumed nonzero-but-not-3 meant FAIL; the JSON `result`
  field remains the authoritative machine signal.

## Invariants

1. PASS requires zero unresolved ownership/lifetime operations in checked scope.
2. No silently ignored `free()`, allocation store, or pointer-return call.
3. Unknown ownership semantics always produce INCOMPLETE, never PASS.
4. Exit codes 0/1/2/3 are stable and semantically fixed.
5. The differential ASan corpus must reject any `ASan violation + cand PASS` pair.
6. Coverage output reports honest counts (functions, tracked heap objects,
   unsupported operations), never modeled-percentage spin.
7. This ADR claims nothing about C&1 soundness; it defines when C& may
   stop claiming.

## Acceptance criteria

ADR-0010 is implemented when:

1. the three reported false negatives return INCOMPLETE, never PASS;
2. every `free()` site classifies as SUPPORTED, KNOWN SAFE, or INCOMPLETE;
3. unknown pointer-return ownership surfaces as `unknown-pointer-return-ownership`;
4. allocation into unmodelled storage surfaces as `allocation-to-untracked-storage`;
5. the differential ASan suite runs in CI and fails on `ASan violation + PASS`;
6. the JSON schema and coverage summary reflect the new obligations;
7. an adversarial review of at least 20 variant programs finds no new
   silent-acceptance hole.
