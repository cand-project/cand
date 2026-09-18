# ADR-0007: Canonical Source Annotation Encoding

- **Status:** Proposed
- **Date:** 2026-09-14
- **Decision owners:** C& maintainers
- **Scope:** `cand.h`, source-level ownership metadata, compatibility

## Context

C& deliberately avoids adding new C grammar. Ownership metadata therefore needs a stable macro/annotation encoding that is:

- accepted by ordinary C compilers in production profile;
- rich enough to express ownership effects;
- visible to the analysis frontend;
- unambiguous across declarations, parameters, and return values;
- versionable without changing the C ABI.

The initial design already distinguishes local ownership, borrows, consumed parameters, destructors, owned returns, safe/unsafe scope, and move markers. Borrowed return values require one extra piece of information: **which input/object lifetime backs the returned pointer**.

## Decision

The canonical v1 source vocabulary SHALL use parameter-local annotations for parameter effects and an indexed function annotation for borrowed return lifetime origin.

Conceptual vocabulary:

| Concept | Canonical source form |
|---|---|
| local owner | `T *p CAND_OWN` |
| shared borrow | `T *p CAND_BORROW` |
| mutable borrow | `T *p CAND_BORROW_MUT` |
| consumed parameter | `T *p CAND_TAKES` |
| destroyed parameter | `T *p CAND_DESTROYS` |
| owned return | function declaration + `CAND_RETURNS_OWN` |
| borrowed return | function declaration + `CAND_RETURNS_BORROW_FROM(n)` |
| checked function | function declaration + `CAND_SAFE` |
| unsafe function/boundary | function declaration + `CAND_UNSAFE` |
| explicit ownership transfer at call | `CAND_MOVE(p)` |

Example:

```c
Header *packet_header(Packet *packet CAND_BORROW)
    CAND_RETURNS_BORROW_FROM(0);

void packet_send(Packet *packet CAND_TAKES);
void packet_free(Packet *packet CAND_DESTROYS);
```

`CAND_RETURNS_BORROW_FROM(0)` means that the returned pointer's lifetime is bounded by parameter 0's backing object/lifetime under the active contract.

## Encoding rule

In analysis profile, indexed annotations SHOULD encode the index into a frontend-visible annotation string, conceptually:

```text
cand:returns_borrow_from:0
```

In production profile, the annotation SHALL expand to no code-generation effect.

The implementation MAY use preprocessor callbacks rather than string parsing internally, but the externally visible meaning is the same.

## Why not a parameterless borrowed-return marker?

A parameterless marker cannot express whether the result borrows from parameter 0, parameter 1, a global/static object, receiver-like state, or another lifetime source.

Strict proof must not guess the backing lifetime.

For APIs where the source lifetime cannot be expressed by a simple parameter index, an external contract SHALL be used until richer source syntax is specified.

## Destructor encoding

C& v1 prefers parameter-local `CAND_DESTROYS` over a function-level `CAND_FREES(n)` spelling because the effect is directly attached to the affected parameter and works naturally with multiple pointer parameters.

A future contract/annotation layer may support conditional destruction or allocator-family information that cannot fit in the simple macro.

## Compatibility aliases

The repository uses the canonical forms above. No parameterless or alternate
borrowed-return spelling is accepted as a P2 lifetime source.

## External contracts remain authoritative for complex APIs

Source annotations are intentionally small. APIs with conditional or complex ownership semantics use machine-readable contracts, including:

- `realloc`-like conditional transfer;
- retained callback/context lifetimes;
- multiple possible lifetime origins;
- out-parameter production;
- reference-counted/shared ownership;
- allocator/deallocator families;
- version-dependent effects.

C& MUST NOT keep extending macros until they become a second programming language.

## Alternatives considered

### `CAND_RETURNS_BORROW` with no lifetime source

**Rejected as canonical strict-proof encoding.** It is under-specified.

### `CAND_FREES(n)` for destructor effects

**Not selected for v1.** Parameter-local `CAND_DESTROYS` is simpler for common cases.

### New C keywords such as `own` and `borrow`

**Rejected by ADR-0001.** Would require parser/compiler language extensions.

### Encode every complex contract in macros

**Rejected.** Complex external behavior belongs in versioned contract files.

## Consequences

### Positive

- borrowed return lifetimes become explicit and machine-readable;
- simple APIs remain easy to annotate;
- complex semantics stay in the contract system;
- production C compatibility is preserved;
- SPEC/header convergence has a clear target.

### Negative

- existing early draft spellings must be cleaned up;
- parameter indices can be brittle if declarations are heavily refactored;
- APIs borrowing from non-parameter state still require external contracts.

## Invariants

1. **Source annotations never change the C ABI.**
2. **Borrowed returns name a proven lifetime origin.**
3. **Complex ownership remains contract-driven rather than macro-driven.**
4. **One canonical spelling is documented for each common effect.**
5. **Strict checking never guesses a missing borrowed-return origin.**

## Acceptance criteria

ADR-0007 is implemented when:

1. `include/cand/cand.h` exposes the canonical v1 spellings;
2. SPEC-0001 uses the same spellings;
3. analysis mode can recover the borrowed-return parameter index;
4. GCC and Clang production-profile compatibility tests remain green;
5. a fixture proves a borrowed return cannot outlive its backing parameter object.
