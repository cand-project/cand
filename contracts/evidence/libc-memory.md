# Provenance: contracts/bundles/libc-memory.yaml (cand-libc-memory)

Reviewed memory-data-movement bundle. Trusted claim per symbol: pointer
arguments are read and/or written only within the call's lifetime; no
retention, destruction, or ownership transfer; returns alias parameter 0.

Primary authoritative source: ISO C11 7.24 (string.h). Implementation
inspected: glibc `string/mem*.c` (SIMD variants all obey the same
within-call read/write contract) and musl `src/string/mem*.c`.

## memcpy

- ISO C11 7.24.2.1: copies `n` characters from s2 to s1; returns s1.
- `void *memcpy(void *restrict s1, const void *restrict s2, size_t n)`.
- Params 0,1 borrow (raw byte read/write within the call); param 2
  (`n`, size_t) no_ownership_effect. Return borrows from param 0.
- Negative/adversarial fixtures: `memchr_borrowed_return_uaf.c` pins the
  borrowed-return class (use of the result after the source buffer is
  freed is a FAIL), and the borrow-parameter class is pinned by
  `tests/interprocedural/borrow_wrapper_uaf.c` plus the bundle conformance
  harness `tests/contracts/run.sh`.

## memmove

- ISO C11 7.24.2.2: as memcpy but overlapping objects are permitted.
- Same classification as `memcpy`.

## memset

- ISO C11 7.24.6.1: fills the first `n` bytes of s with `c`; returns s.
- `void *memset(void *s, int c, size_t n)`.
- Param 0 borrow (write within the call); params 1 (`c`, int) and 2 (`n`)
  no_ownership_effect. Return borrows from param 0.

## memcmp

- ISO C11 7.24.4.1: compares the first `n` bytes; returns int.
- Params 0,1 borrow; param 2 no_ownership_effect. Scalar return: no
  returns section.

## memchr

- ISO C11 7.24.5.1: locates the first occurrence of `c` in the first `n`
  bytes of s; returns a pointer into s or a null pointer.
- Param 0 borrow; params 1,2 no_ownership_effect. Return borrows from
  param 0 (interior pointer into the caller's buffer — this is the exact
  boundary fact that keeps the Hiredis `read.c` line scanner's B003
  detections intact; see the CVE replay interaction results).

Note on this milestone's audit finding: the previous libc-borrow bundle
omitted the scalar parameter positions of these symbols, so escape
obligations fired whenever an argument expression merely contained a
tracked pointer (for example `sizeof(*p)`). The scalar positions are now
listed explicitly; the pointer classifications are unchanged.
