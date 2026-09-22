# External API trust model (C&1/v1)

Status: normative for the reviewed external-API contract bundles introduced by
milestone #58. This document defines what a contract entry claims, why each
accepted claim is sound, what may never be claimed, and how wrong claims are
prevented and detected.

The C&1 authority chain is unchanged: SPEC-0010 defines Trustworthy PASS; the
verifier binary, its policy, and the trusted contract set together form the
proof authority. **A contract entry is part of the trusted computing base.**
A wrong contract can create a false PASS, so every entry carries provenance,
a review requirement, and executable conformance evidence.

## 1. What a contract entry claims

A symbol entry in a reviewed bundle asserts, for the exact upstream API it
names:

- for each parameter position: what the callee may do with the argument
  value and (for pointer arguments) with the pointee, and whether any
  ownership or lifetime authority crosses the boundary;
- for the return value: whether it introduces new ownership, aliases a
  parameter, points into static storage, or is a non-pointer.

The claim is about the **callee as a black box**. It must be justified from
authoritative documentation and signature inspection, not from how one
project happens to call it.

## 2. Accepted claim classes

### BORROW DURING CALL (`effect: borrow`)

The callee may read and/or write the pointee's raw bytes only within the
call's lifetime. No destruction, no retention beyond the call, no ownership
transfer. Borrow is exact for buffers handed to I/O, comparison, copy, and
option-argument APIs (e.g. `memcpy`, `send`, `setsockopt` optval).

### BORROWED RETURN FROM PARAMETER (`ownership: borrowed` + `from_param: N`)

The return value aliases the documented input parameter (or NULL); no new
ownership is introduced (e.g. `memchr`, `strchr`, `strncpy`).

### NO OWNERSHIP EFFECT (`effect: no_ownership_effect`)

**Sound only where the C type system forbids passing a pointer at that
position** (by-value scalar integer/enum/floating parameters such as sizes,
flags, characters, file descriptors). The argument is one of:

- a plain scalar: the callee receives a copy and can retain nothing it never
  receives;
- an expression that *contains* a tracked pointer (`sizeof(*p)`, `*p`,
  `p[i]`, `c->fd`): the sub-expression is evaluated on the **caller side**
  and validated by the ordinary expression walk (the analyzer recurses into
  call arguments; dereferences and member accesses go through `checkAccess`),
  and the callee still only receives the scalar value.

It is **never sound** for by-value struct parameters (a struct may contain
pointer members the callee could retain), for pointer parameters, or for
pointer-to-pointer positions.

### DESTROY / CONSUME (`effect: destroys` / `consumes`)

Only when the API contract is explicit and unconditional (e.g. `free`).
Not extended in this milestone beyond the already-reviewed allocator bundle.

## 3. Excluded classes (fail-closed, INCOMPLETE)

The following are deliberately NOT contracted. Each exclusion has a concrete
mechanism, not just caution:

- **Static / thread-local returns** (`strerror`, `__errno_location`,
  `__ctype_b_loc`, `gai_strerror`): the return points at storage whose
  lifetime is owned by the library; no accepted class can express it. A
  wrong `borrow_from_arg` here would fabricate a lifetime relation.
- **Pointer-to-pointer output** (`strtol` endptr, `accept` sockaddr*, any
  `T **out`): a contract of *any* shape on such a function activates the
  summary path and thereby **suppresses the fail-closed
  `unknown-call-with-pointer-output` obligation** for the unmodelled
  position. There is no parameter effect in the qualified schema that
  re-creates it. Therefore functions with pointer-to-pointer parameters
  stay uncontracted entirely. (This is the #41 boundary.)
- **Out-owner allocation** (`getaddrinfo`, `freeaddrinfo`): conditional and
  family-specific ownership production; SPEC-0003 territory.
- **realloc-like lifetime replacement**: path-sensitive; already
  special-cased fail-closed in the verifier.
- **va_list / arbitrary vararg ownership** (`va_start`, `va_end`,
  `va_copy`, and variadic positions of `snprintf`/`fcntl`): variadic state
  objects have implementation-defined lifetime semantics. The verifier
  caps contract parameter lists at the declared fixed parameters, so
  variadic positions keep their fail-closed escape obligations (the
  incident-#53 repair) regardless of any contract.
- **Callback/context retention** (async event loops, qsort comparators):
  the callee stores the pointer beyond the call.
- **Conditional ownership by return code**: not representable.

## 4. Completeness rule

Unlisted parameter positions default to the fail-closed `unknown` effect:
the analyzer emits an escape obligation whenever the argument expression
contains tracked storage. Consequences:

1. **Every parameter position of a contracted symbol must be listed
   explicitly.** Partial coverage is not a soundness hole (it is
   conservative), but it produces spurious obligations and hides which
   boundary fact is missing; `scripts/contracts/check_bundles.py` rejects
   incomplete entries.
2. **Every pointer-typed parameter must carry an explicit ownership
   classification** (borrow / destroys / consumes), never
   `no_ownership_effect`.
3. A contract on a pointer-returning function must state its return
   ownership; otherwise the loader records an unknown return and the call
   stays INCOMPLETE.

## 5. Review requirements (per symbol)

A symbol may enter a reviewed bundle only with all of:

1. authoritative API documentation (standard or POSIX page) cited in the
   provenance record;
2. header/signature inspection (parameter count, order, types);
3. implementation inspection where practical (e.g. glibc/musl source);
4. an explicit ownership/lifetime statement in the provenance record;
5. a negative/adversarial fixture in the conformance harness;
6. reviewer-visible rationale and a bundle diff small enough to review.

Provenance records live in `contracts/evidence/<bundle>.md` (one section per
symbol) and the merged-bundle digest is recorded in every measurement that
uses it.

The agent policy remains: LLM-generated or candidate contracts are
untrusted; a candidate bundle can never replace a reviewed bundle inside an
authoritative run (see `contracts/agent-policy.yaml`:
`candidate_contract_generation: allowed`, `trusted_contract_promotion:
forbidden`).

## 6. Bundle architecture and merge

- Source bundles live in `contracts/bundles/*.yaml`, one per API domain,
  each with a stable `name` and `version`.
- `contracts/libc.yaml` (allocator family) is merged unchanged.
- `scripts/contracts/merge_contracts.py` deterministically concatenates the
  bundle list into the single file `cand check --contracts` accepts. It:
  - rejects duplicate symbols across bundles (exit nonzero; no last-wins);
  - rejects malformed bundles (missing headers, wrong schema, content
    outside symbol entries; the inner per-parameter format is validated by
    `check_bundles.py` and the verifier's own loader);
  - emits the SHA-256 of every input bundle and of the merged file for
    evidence records;
  - produces byte-identical output for identical inputs.
- The merged file is a build product: it is **never committed**; every
  harness (CVE replay, pilots, tests) regenerates it and records its digest.
  This prevents a candidate bundle from silently replacing the reviewed set
  and prevents drift between the merged artifact and the reviewed sources.

## 7. False-PASS response

`confirmed temporal violation + authoritative C&1 PASS = 0` is the hard
invariant. If any harness (CVE replay, conformance fixture, pilot run)
observes a MISSED:

1. suspend the affected claim and the bundle version involved;
2. remove or narrow the offending contract;
3. add a regression fixture reproducing the incident;
4. re-run the full qualification gates before the claim is restored.

## 8. Measurement discipline

Every before/after measurement (pilot, CVE replay, fuzz) records:

- the exact verifier binary digest;
- the exact bundle list and the merged-bundle digest;
- obligation counts, findings, CLEAR/BLOCKED function counts;
- any lost CLEAR or added obligation (both are regressions and stop the
  milestone).

See `docs/pilots/EXTERNAL-CONTRACT-ADOPTION-RESULTS.md` for the measured
results of this milestone.
