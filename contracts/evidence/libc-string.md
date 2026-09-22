# Provenance: contracts/bundles/libc-string.yaml (cand-libc-string)

Reviewed string-inspection bundle. Trusted claim per symbol: pointer
arguments are read (and for strncpy written) only within the call's
lifetime; returns alias a documented parameter.

Primary authoritative source: ISO C11 7.24; `strcasecmp`/`strncasecmp`
from POSIX.1-2017 (strings.h). Implementation inspected: glibc and musl
`src/string/*`.

## strlen

- ISO C11 7.24.6.3: computes the length of the string; returns size_t.
- Param 0 borrow. Scalar return.

## strnlen

- POSIX.1-2017 strlen(3p) strnlen: as strlen but scans at most `maxlen`.
- Param 0 borrow; param 1 (`maxlen`) no_ownership_effect.

## strchr

- ISO C11 7.24.5.2: locates the first occurrence of `c` (converted to
  char) in the string; returns a pointer into the string or NULL.
- Param 0 borrow; param 1 (`c`, int) no_ownership_effect — the common
  idiom `strchr(set, *p)` reads `*p` caller-side, which the ordinary
  expression walk validates. Return borrows from param 0.

## strncmp

- ISO C11 7.24.4.4: compares not more than `n` characters; returns int.
- Params 0,1 borrow; param 2 no_ownership_effect.

## strncpy

- ISO C11 7.24.2.4: copies at most `n` characters from s2 to s1; returns s1.
- Params 0,1 borrow; param 2 no_ownership_effect. Return borrows param 0.

## strcasecmp

- POSIX.1-2017 strcasecmp(3p): case-insensitive comparison; returns int.
- Params 0,1 borrow.

## strncasecmp

- POSIX.1-2017 strncasecmp(3p): as strcasecmp, at most `n` bytes.
- Params 0,1 borrow; param 2 no_ownership_effect.

Deliberately excluded from this bundle (fail-closed): `strcpy`, `strcat`
(unbounded writes; no measured demand), `strdup`/`strndup` (new
allocations; allocator family, separate review), `strtok` (retains the
pointer across calls in static state), `strerror` (static-storage return).
