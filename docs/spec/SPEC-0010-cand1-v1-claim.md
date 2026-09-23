# SPEC-0010 — C&1/v1 Qualified Temporal Ownership Claim

Status: normative qualification boundary; C&1/v1 qualified on protected main.

Qualified reviewed HEAD: `7a6f4b65fb6e7506d073f9c93aa615c0e6bf8860`
Merge/release commit: `3a2672b6b6a6742c8ac19c2894a698cdd1970b7a`

## 1. Guarantee

For a generated verification run that satisfies this specification, `cand1 PASS`
means that every ownership/lifetime operation represented in the checked scope
was analyzed under the qualified C&1/v1 rules, no represented violation was
found, and no unsupported or policy-invalid operation contributed to PASS.
The guarantee is about the represented program paths and storage identities; it
is not a claim about arbitrary C.

## 2. Checked scope

The checked scope is the exact source-file list in the generated policy and the
files reached by its accepted frontend invocation. Evidence records the source
commit, every checked file digest, policy digest, frontend arguments, and rule
sets. A scope outside that binding is not covered. A missing, changed, or
unreplayable scope is stale or incomplete, never PASS.

## 3. Qualified profile

C&1/v1 supports Ubuntu 24.04 x86_64, C11 with extensions disabled, target
`x86_64-pc-linux-gnu`, default sysroot identity `ubuntu-24.04-default`,
Clang/LLVM 18.1.3, production Clang 18.1.3, production GCC 13.3.0, CMake
3.28.3, and `/usr/bin/ninja` 1.11.1. Other hosts, versions, targets,
standards, sysroots, extensions, plugins, response files, and external include
roots are outside the profile and must fail closed.

## 4. Semantic rules

1. Unique ownership is one live ownership capability for an object identity.
2. A move transfers that capability and invalidates the moved-from capability.
3. Destruction transitions the object generation to dead; repeated or
   non-owner destruction is a violation.
4. A use after destruction, use after move, stale-generation use, or invalid
   destruction is a known violation and returns FAIL.
5. Shared borrows refer to a live parent generation and become invalid when the
   parent dies.
6. Mutable borrows are exclusive with other mutable borrows, shared borrows,
   and conflicting owner access while their modeled lifetime is live.
7. Backward liveness may expire a borrow after its last modeled use. Missing
   liveness information is conservative and remains live or becomes
   UNSUPPORTED; it cannot create PASS for an unresolved operation.
8. CFG joins use deterministic ownership/borrow lattice joins. Maybe-dead,
   maybe-moved, or maybe-invalid states cannot silently become safe.
9. Loop allocation sites use generations. A reference from an earlier
   generation cannot be associated with a later generation without proof.
10. Same-translation-unit bodies may contribute deterministic summaries for
    supported calls, wrappers, returns, and parameter transport.
11. External effects require a builtin, verified, or separately reviewed
    trusted contract, or a declaration whose ownership annotations are
    confirmed by a separately reviewed annotation-review manifest
    (`cand.annotation-review/v1`, ADR-0029) with exactly equal facts.
    Unknown, indirect, callback, retention, and conflicting effects fail
    closed. Candidate-only annotations — no manifest, absent symbol, fact
    mismatch, or conflicting redeclaration facts — fail closed and report
    an `unreviewed-declaration-annotation` or
    `conflicting-declaration-annotation` obligation. Visible bodies are
    never overridden by annotations or manifests.
12. General cross-translation-unit semantic analysis is not part of v1. A
    cross-TU fact without an accepted contract is UNSUPPORTED/INCOMPLETE.
13. Aggregate, alias, pointer, global, out-parameter, and transport forms are
    supported only where the implementation emits a classified result. Any
    unclassified ownership-relevant form is UNSUPPORTED/INCOMPLETE.

## 5. PASS authority

Only the generated-profile path may emit authoritative C&1 PASS. The centralized
predicate in `src/cand.cpp` (`canEmitCand1Pass`) requires zero findings, zero
unsupported operations, zero frontend/contract errors, zero transport
uncertainty, bound evidence, no policy failure or weakening, no review-required
delta, a supported toolchain, generated/cand1 policy, non-empty scope and
policy digest, trusted contracts only, and zero unsafe boundaries/suppressions.

Semantic `pass` without these bindings is not a C&1 PASS.

## 6. Unsupported and unsafe behavior

Every ownership/lifetime-relevant result is one of SUPPORTED / KNOWN SAFE,
KNOWN VIOLATION -> FAIL, UNSUPPORTED -> INCOMPLETE, or TOOL/POLICY ERROR.
Explicit unsafe or unsupported regions cannot contribute to authoritative PASS.

## 7. Evidence and release qualification

Evidence must bind verifier source and binary digests, analyzed source identity,
frontend arguments and identity, C++ build compiler identity/path/version,
CMake/Ninja path/version, target, C11, sysroot, reference environment digest,
policy, contracts, checked scope, and semantic rule sets. Replay must verify the
integrity digest, exact identities, current source/policy/contracts, and rerun
the recorded frontend invocation.

Release qualification requires the normative matrix, independent adversarial
corpus, sanitizer differential, extended fuzzing, fail-closed policy/evidence
attacks, cross-TU boundary tests, exact toolchain qualification, two identical
clean builds, exact-head CI, and reviewed evidence. Confirmed temporal violation
plus C&1 PASS must equal zero.

## 8. False-PASS response

A confirmed false PASS is a C&1 soundness incident. The public claim is
suspended, affected evidence and release identity are revoked, the case is
minimized and permanently regressed, the root cause is fixed, and the complete
exact-head qualification is rerun before any claim restoration. See
`docs/CAND1-FALSE-PASS-RESPONSE.md`.

## 9. Explicit non-claims

C&1/v1 does not claim spatial/bounds safety, arbitrary pointer-arithmetic
safety, general null safety, integer safety, general pointer/integer provenance
safety, arbitrary inline-assembly correctness, general concurrency or data-race
safety, correctness inside explicit unsupported/unsafe regions, unsupported
compiler extensions, or semantics outside the qualified toolchain/profile.
It also does not claim that all C is memory-safe, that all temporal bugs are
impossible, or that the result is equivalent to Rust safety.
