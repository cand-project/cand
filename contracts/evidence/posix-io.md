# Provenance: contracts/bundles/posix-io.yaml (cand-posix-io)

Reviewed POSIX file-descriptor bundle.

Primary authoritative source: POSIX.1-2017 (The Open Group Base
Specifications Issue 7) 12.2. Implementation inspected: Linux kernel
`fs/close.c` (file descriptor release), glibc `sysdeps/posix/close.c`.

## close

- POSIX.1-2017 close(3p): deallocates the file descriptor; returns int.
- `int close(int fildes)` — param 0 by-value integer descriptor.
- no_ownership_effect: the C type system forbids passing a pointer at an
  `int` parameter. The kernel releases the descriptor's resources but no
  C pointer object in the caller changes ownership; buffer lifetimes in
  the caller are untouched. The idiom `close(c->fd)` reads `c->fd`
  caller-side and is validated by the ordinary expression walk.
- Negative/adversarial fixture: `close_member_uaf.c` in the conformance
  harness (a dangling `c` read inside `close(c->fd)` is still found).

Excluded (fail-closed, no measured demand yet — add with fresh evidence
first): `read`/`write` (buffer positions are borrow-shaped), `fcntl`
(variadic third argument; F_SETLK passes a struct flock*), `dup`/`dup2`,
`pipe`, all `FILE*` stdio (the stream pointer is retained across calls).
