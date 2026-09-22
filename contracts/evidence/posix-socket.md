# Provenance: contracts/bundles/posix-socket.yaml (cand-posix-socket)

Reviewed POSIX socket-option and data bundle. Trusted claim per symbol:
pointer arguments are read and/or written only within the call's lifetime;
the socket layer may copy bytes into kernel buffers during the call but
never retains the caller's buffer, takes ownership, or destroys anything.

Primary authoritative source: POSIX.1-2017 (The Open Group Base
Specifications Issue 7) 12.9-12.10. Implementation inspected: Linux kernel
`net/socket.c` (`sock_sendmsg`/`sock_recvmsg`/`sock_setsockopt`/
`sock_getsockopt`/`__sys_connect`) — all copy option values and buffers
to/from kernel stack copies within the syscall.

## connect

- POSIX.1-2017 connect(3p): initiates connection on a socket using the
  address in `address` of length `address_len`.
- `int connect(int socket, const struct sockaddr *address, socklen_t address_len)`.
- Param 0 (int) and param 2 (socklen_t) no_ownership_effect; param 1
  borrow (address bytes copied into the kernel during the call).

## setsockopt

- POSIX.1-2017 setsockopt(3p): sets the option `option` at level `level`
  from `option_value` of length `option_len`.
- `int setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len)`.
- Params 0,1,2,4 scalar no_ownership_effect; param 3 borrow.

## getsockopt

- POSIX.1-2017 getsockopt(3p): retrieves the option value into
  `option_value` and its length into `option_len`.
- `int getsockopt(int socket, int level, int option_name, void *restrict option_value, socklen_t *restrict option_len)`.
- Params 0,1,2 scalar no_ownership_effect; params 3,4 borrow (both are
  read/written within the call; param 4's pointee is a scalar length,
  never a pointer, so borrow is exact and no pointer-output is granted).

## send

- POSIX.1-2017 send(3p): sends a message through a connected socket;
  message is read from `buffer` of length `length`.
- Params 0,2,3 scalar no_ownership_effect; param 1 borrow.

## recv

- POSIX.1-2017 recv(3p): receives a message into `buffer`.
- Params 0,2,3 scalar no_ownership_effect; param 1 borrow (written within
  the call).

Excluded (fail-closed): `socket`/`bind`/`listen` (no measured demand),
`accept` (writes a `sockaddr *restrict` through a pointer-to-pointer),
`getaddrinfo`/`freeaddrinfo` (out-owner allocation family, SPEC-0003),
`sendto`/`recvfrom`/`shutdown` (no measured demand yet).
