# SPEC-0006 — C&1 temporal ownership verification profile

Status: Draft release gate (C&1-A)

## 1. Purpose

`cand1/v1` is the first strict C& verification profile. It is a verification
contract, not a claim that arbitrary C is memory safe.

For a checked scope accepted under `cand1/v1`, C& verifies temporal object
lifetime, unique ownership transfer, destruction authority, explicit borrow
lifetime, and the documented whole-object shared/mutable borrow constraints
for the supported semantics below.

The profile is not enabled as a public C&1 safety claim by this specification.
C&1-A qualifies the contract and its heap abstraction; a later release gate
must authorize the claim.

## 2. Result vocabulary

Every checked scope has exactly one result:

* `PASS`: the strict acceptance predicate is true.
* `FAIL`: a modeled semantic or proof-policy violation is present.
* `INCOMPLETE`: an ownership/lifetime-relevant operation or proof obligation
  is outside the supported model, or the verifier widened to an unknown state.
* `REVIEW_REQUIRED`: trusted inputs or checked scope require human review.
* `TOOL_ERROR`: the source, frontend, verifier, or evidence pipeline failed.

`INCOMPLETE` is never converted to `PASS` by omission or by a fallback mode.

## 3. C&1 guarantee

Within the checked scope and supported semantics, a `PASS` establishes that
C& found no modeled instance of:

* use after object destruction or double destruction;
* use after move or destruction through non-owner storage;
* loss or conflict of unique ownership where modeled;
* explicit borrow use after its parent object lifetime;
* owner destruction with a live modeled borrow;
* borrow escape beyond its declared parent lifetime;
* supported shared/mutable whole-object borrow conflicts; or
* unsupported interprocedural ownership/borrow effects.

The guarantee is temporal and ownership-oriented. It does not establish
buffer bounds safety, integer-overflow safety, uninitialized-memory safety,
data-race or general thread safety, general pointer-provenance correctness,
arbitrary pointer arithmetic safety, compiler-extension or inline-assembly
safety, or safety across unmodeled foreign-language boundaries.

## 4. Strict acceptance predicate

There is one authority for emitting a C&1 `PASS`:

```text
can_emit_cand1_pass(report, evidence, policy)
```

It is true only when all of the following hold:

```text
semantic violations                         == 0
unsupported ownership operations            == 0
unsupported ownership transfers             == 0
unsupported borrow operations               == 0
unresolved ownership/lifetime summaries     == 0
untrusted contracts used as proof           == 0
proof-policy weakening                      == false
unsafe/suppression budgets                  are respected
required checked files                      are present and exact
verifier/source/policy/contract identities  are bound and trusted
frontend/toolchain identities               are supported and bound
evidence replay                             is valid
```

The predicate also requires a successful frontend and a converged analyzer.
Any missing input is `TOOL_ERROR`, `INCOMPLETE`, or `REVIEW_REQUIRED`, never a
successful default.

## 5. Supported heap identity

An object identity is an abstract heap instance, not an allocation-site name.
It is derived from:

```text
(function context, allocation site, generation)
```

Generation zero is the first abstract instance at a site. A later instance
may reuse the site's stable identity only after the prior instance is dead and
no tracked storage or borrow can still refer to it. If a prior reference can
survive, C& creates a deterministic later generation. If generation identity
would become unbounded or ambiguous at a loop, recursive call, or join, the
state widens to `Unknown` and the result is `INCOMPLETE`.

No two runtime objects are identified solely because they share an AST
allocation expression. A stale alias must therefore never become valid merely
because a later loop iteration reuses that expression.

## 6. Scope of C&1-A

C&1-A qualifies the profile contract and repeated-allocation heap model. It
does not complete the transport-boundary audit, reproducible toolchain gate,
or differential/protocol fuzzing gate. Those remain C&1-B/C/D work and keep
the public C&1 claim disabled.
