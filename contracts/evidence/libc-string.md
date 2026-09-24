# Provenance: contracts/bundles/libc-string.yaml (cand-libc-string)

Reviewed string-inspection bundle. Trusted claim per symbol: pointer
arguments are read (and for strncpy/strlcpy written) only within the
call's lifetime; returns alias a documented parameter or are scalars.

Primary authoritative source: ISO C11 7.24; `strcasecmp`/`strncasecmp`
from POSIX.1-2017 (strings.h); `strlcpy` from POSIX.1-2024 (OpenBSD
origin). Implementation inspected: glibc and musl `src/string/*`.

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

## strrchr

- ISO C11 7.24.5.5: locates the last occurrence of `c` in the string;
  returns a pointer into the string or NULL. Same claim class as
  strchr.
- Param 0 borrow; param 1 no_ownership_effect. Return borrows from
  param 0.

## strstr

- ISO C11 7.24.5.7: locates the first occurrence of the substring
  `needle` in `haystack`; returns a pointer into `haystack` or NULL.
- Params 0,1 borrow. Return borrows from param 0 (never from needle).

## strpbrk

- ISO C11 7.24.5.4: locates the first occurrence in `s1` of any
  character from `s2`; returns a pointer into `s1` or NULL.
- Params 0,1 borrow. Return borrows from param 0.

## strcmp

- ISO C11 7.24.4.2: compares two strings; returns int. Same claim
  class as strncmp without the length parameter.
- Params 0,1 borrow. Scalar return.

## strncmp

- ISO C11 7.24.4.4: compares not more than `n` characters; returns int.
- Params 0,1 borrow; param 2 no_ownership_effect.

## strspn

- ISO C11 7.24.5.6: computes the length of the initial segment of `s1`
  consisting of characters from `s2`; returns size_t.
- Params 0,1 borrow. Scalar return.

## strcspn

- ISO C11 7.24.5.3: computes the length of the initial segment of `s1`
  consisting of characters not from `s2`; returns size_t.
- Params 0,1 borrow. Scalar return.

## strncpy

- ISO C11 7.24.2.4: copies at most `n` characters from s2 to s1; returns s1.
- Params 0,1 borrow; param 2 no_ownership_effect. Return borrows param 0.

## strcasecmp

- POSIX.1-2017 strcasecmp(3p): case-insensitive comparison; returns int.
- Params 0,1 borrow.

## strlcpy

- POSIX.1-2024 (OpenBSD origin; glibc >= 2.38 and musl strlcpy(3)):
  copies at most `size`-1 bytes from `src` to `dst`, NUL-terminating
  the result; returns the length of `src` (size_t). The write to
  `dst` is bounded and confined to the call, exactly the strncpy
  claim class; unlike strncpy the return is a scalar, not `dst`.
- Params 0,1 borrow; param 2 no_ownership_effect. Scalar return.

## strncasecmp

- POSIX.1-2017 strncasecmp(3p): as strcasecmp, at most `n` bytes.
- Params 0,1 borrow; param 2 no_ownership_effect.

Deliberately excluded from this bundle (fail-closed): `strcpy`, `strcat`
(unbounded writes; no measured demand), `strdup`/`strndup` (new
allocations; allocator family, separate review), `strtok` (retains the
pointer across calls in static state), `strerror` (static-storage
return; the largest measured libc demand at 151 obligation rows —
representing it needs a reviewed static-storage return class, not this
bundle), `strsep`/`strtok_r` (pointer-to-pointer state output), the
`strtol` family (pointer-to-pointer `endptr` output; #41 boundary),
`strerror_r` (variant-dependent GNU vs POSIX return semantics),
`strsignal` (implementation-defined storage).

Measured demand recorded in the milestone #36 E2 re-measurement
(docs/pilots/CAND1-REAL-WORLD-BASELINE.md, "Cause slots and
next-milestone inputs"): strcmp 104 rows, strerror 151, strrchr 22,
strstr 17, strtoll 11, strsep 10, strtok_r 9 across the eight measured
pilots at E2; strspn/strcspn/strpbrk/strlcpy 3 each, memrchr 4.
