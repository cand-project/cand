# Provenance: contracts/bundles/libc-ctype.yaml (cand-libc-ctype)

Reviewed character-classification bundle.

Primary authoritative source: ISO C11 7.4 (ctype.h). Implementation
inspected: glibc `ctype/ctype-info.c` and `__ctype_b_loc` usage — the
functions themselves only index static lookup tables by the argument
value; they never dereference it as a pointer.

## tolower

- ISO C11 7.4.2.1: converts an uppercase letter to lowercase; argument is
  an `int` representable as unsigned char or EOF; returns int.
- Param 0 no_ownership_effect: the C type system forbids passing a pointer
  at this position. The idiom `tolower(*p)` reads `*p` caller-side; that
  read is validated by the ordinary expression walk (checkAccess on the
  deref), so no temporal violation can be hidden by this entry.
- Negative/adversarial fixture: `ctype_scalar_uaf.c` in the conformance
  harness (a use-after-free read through `*p` inside a `tolower(*p)`
  argument is still found).

## toupper

- ISO C11 7.4.2.2: as tolower, uppercase direction.
- Same classification and soundness argument.

Excluded (fail-closed): `__ctype_b_loc` (returns a pointer to thread-local
table storage; no accepted class), `isalpha`-family (same shape as
tolower but no measured demand yet; add with fresh evidence first).
