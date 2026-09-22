# Provenance: contracts/libc.yaml (cand-libc-core)

Reviewed allocator-family bundle. Entries originally reviewed with the
v0.2.x qualification; this milestone's audit added explicit scalar parameter
coverage (see the trust model, section 4). `malloc`/`calloc` params are
inert for verdicts (the verifier routes them through the allocator fast
path before contract summaries are consulted); `realloc` param 1 is live.

## malloc

- Authoritative doc: ISO C11 7.22.3.4 — allocates space for an object of
  `size` bytes; returns either a null pointer or a unique pointer.
- Signature: `void *malloc(size_t size)` — param 0 by-value scalar.
- Ownership/lifetime: returns a uniquely-owned heap pointer (or NULL);
  no parameter observability. allocation_family c-heap.
- Negative/adversarial fixture: `tests/contracts/fixtures/`
  (`free_then_use_malloc.c`; `realloc_use_after.c` pins the fail-closed
  old-object behavior).
- Rationale: `no_ownership_effect` on the size parameter is sound because
  the C type system forbids passing a pointer at a `size_t` position; any
  tracked-pointer read in the argument expression is caller-side and
  validated by the ordinary expression walk.

## calloc

- Authoritative doc: ISO C11 7.22.3.1 — allocates space for `nmemb` objects
  of `size` bytes each, zero-initialized.
- Signature: `void *calloc(size_t nmemb, size_t size)`.
- Ownership/lifetime: same class as `malloc`.
- Negative/adversarial fixture: as `malloc`.

## free

- Authoritative doc: ISO C11 7.22.3.3 — deallocates the object; the value
  of the pointer becomes indeterminate.
- Signature: `void free(void *ptr)` — param 0 object pointer, destroyed
  unconditionally.
- Ownership/lifetime: destroys the pointee (allocation family c-heap).
- Negative/adversarial fixture: `free_then_use_malloc.c` proves
  use-after-free still fails after the call.

## realloc

- Authoritative doc: ISO C11 7.22.3.5 — deallocates the old object and
  returns a pointer to a new object; the old pointer is unusable whether or
  not the call succeeds.
- Signature: `void *realloc(void *ptr, size_t size)` — param 0 consumed,
  param 1 by-value scalar.
- Ownership/lifetime: the *verifier* deliberately downgrades this entry's
  return to unknown (path-sensitive success/failure); the entry records the
  intended semantics and stays fail-closed. Param 1 `no_ownership_effect`
  is sound as for `malloc`.
- Negative/adversarial fixture: `realloc_use_after.c` proves the old-object
  use-after-realloc remains an obligation/finding, not a PASS.
