# External API boundary Pareto analysis (Hiredis E0)

Status: evidence base for milestone #58 (reviewed external API boundary
library). This document records the precise, callee-resolved attribution of
Hiredis E0 obligations to blocking families, the per-symbol external-API
inventory, the unlock ranking that drove bundle selection, and the measured
outcome. It supersedes the coarser split in
`docs/pilots/CAND1-ADOPTION-BLOCKER-PARETO.md` (see "corrections" below).

Measurement frame:

- verifier: exact main `3fc8563`, binary digest
  `e5ee4952b01cbd78ea1e00404e732eb78b54c898dda3a6d17b8ddbab0e89fd14`
- project: `redis/hiredis@33a12fb`, 7 TUs (alloc, async, hiredis, net,
  read, sds, sockcompat), `-std=gnu11` (glibc `assert` expands with
  statement expressions; `-std=c11` yields 887 obligations instead of 915 —
  a method note, not a verifier difference)
- contracts (E0): `contracts/libc.yaml` + `contracts/libc-borrow.yaml`
  merged (the pre-milestone reviewed set)
- result: **915 obligations, 4 findings (CAND-B003), 181 functions:
  18 CLEAR / 163 BLOCKED**

## Corrections to earlier numbers

1. **Function universe**: the earlier addendum counted 179 functions /
   17 CLEAR. The AST mapping used there missed two functions
   (`sdsHdrSize`, `__redisReaderSetError`); cand's own
   `coverage.functions_analyzed` is 181. Corrected E0 baseline:
   **18 CLEAR / 163 BLOCKED**.
2. **Sole-external-blocked count**: the addendum's "23 functions solely
   blocked on external APIs" was attribution error (obligations whose kind
   suffix named a *callee* were credited to the callee's family). The
   corrected sole-EXT count is **7 functions** (below).
3. **An earlier ad-hoc E1 delta** ("+17 CLEAR") was a mapping artifact of
   an unreviewed script. The corrected, obligation-diff-verified delta is
   in `docs/pilots/EXTERNAL-CONTRACT-ADOPTION-RESULTS.md`.

## Family attribution (E0, callee-resolved)

| Family | Meaning | Obligations | Functions containing | Sole blocker |
|---|---|---|---|---|
| XTU | cross-TU project callee, no summary | 210 | 66 | 19 |
| ALIAS | alias/storage precision | 205 | 71 | 6 |
| STU | same-TU callee with undecided body | 199 | 97 | 24 |
| EXT | external API boundary (libc/POSIX/external, categories A-C) | 117 | 38 | 7 |
| OTHER | statement expressions, pointer arithmetic, static storage | 115 | 50 | 1 |
| IND | indirect/callback calls | 69 | 22 | 4 |
| **total** | | **915** | 163 blocked | 61 sole-blocked |

Most Hiredis functions are blocked by *several* families at once; clearing
EXT alone can therefore only clear a function that is solely EXT-blocked.
This is the structural reason the external-boundary library has a bounded
effect on this codebase, and why the cross-family blockers (XTU summaries,
alias precision) dominate the remaining Pareto.

## External symbol inventory (E0, 24 symbols, 117 obligations)

| Symbol | Cat | Sites | Functions | Kinds |
|---|---|---|---|---|
| `__errno_location` | B | 27 | 10 | pointer-return-ownership |
| `__builtin_va_end` | B | 10 | 8 | pointer-output |
| `__builtin_va_start` | B | 6 | 6 | pointer-output + tracked-pointer |
| `memcpy` | A | 4 | 4 | tracked-pointer |
| `__ctype_b_loc` | B | 9 | 4 | pointer-return-ownership |
| `strerror` | A | 8 | 4 | pointer-return-ownership |
| `setsockopt` | B | 5 | 4 | tracked-pointer |
| `strchr` | A | 9 | 2 | tracked-pointer |
| `tolower` | A | 2 | 2 | tracked-pointer |
| `__builtin_va_copy` | B | 2 | 2 | tracked-pointer + pointer-output |
| `memset` | A | 2 | 2 | tracked-pointer |
| `connect` | B | 2 | 2 | tracked-pointer |
| `getsockopt` | B | 2 | 2 | tracked-pointer |
| `strtol` | A | 1 | 1 | pointer-output + tracked-pointer |
| `strerror_r` | A | 1 | 1 | tracked-pointer |
| `close` | B | 1 | 1 | tracked-pointer |
| `recv` | B | 1 | 1 | tracked-pointer |
| `send` | B | 1 | 1 | tracked-pointer |
| `snprintf` | A | 1 | 1 | tracked-pointer |
| `fcntl` | B | 2 | 1 | tracked-pointer |
| `getaddrinfo` | B | 3 | 1 | tracked-pointer + pointer-output |
| `gai_strerror` | B | 2 | 1 | pointer-return-ownership |
| `freeaddrinfo` | B | 3 | 1 | pointer-output |
| `toupper` | A | 1 | 1 | tracked-pointer |

Categories: A = ISO C, B = POSIX/system/compiler runtime, C = external
library.

### Root cause of most EXT obligations

89 of the 117 EXT rows sit at symbols that were *already contracted* in the
pre-milestone set (memcpy, memset, strchr, snprintf, connect, setsockopt,
...). The pre-milestone `libc-borrow` bundle listed only pointer parameter
positions; unlisted positions default to the fail-closed
`ParamEffect::Unknown`, so an escape obligation fires whenever the argument
expression merely *contains* a tracked pointer (`sizeof(*p)`, `*p`,
`c->fd`). Completing parameter coverage — including by-value scalar
positions as `no_ownership_effect` — is therefore both a soundness-neutral
and a measurement-correcting repair; it is enforced structurally by
`scripts/contracts/check_bundles.py` (completeness rule).

## Sole-EXT functions and unlock ranking

The 7 functions blocked *only* by external-API boundary obligations:

| Function | External symbols involved | Clearable by reviewed bundles? |
|---|---|---|
| `redisContextUpdateCommandTimeout` | memcpy | **yes** (measured) |
| `redisContextUpdateConnectTimeout` | memcpy | **yes** (measured) |
| `redisNetClose` | close | **yes** (measured) |
| `sdstolower` | tolower | **yes** (measured) |
| `sdstoupper` | toupper | **yes** (measured) |
| `redisCheckConnectDone` | `__errno_location`, connect, getsockopt | **no**: `__errno_location` returns thread-local storage — excluded class, stays fail-closed |
| `chrtos` | `__ctype_b_loc` | **no**: thread-local table return — excluded class |

The ranking predicted the measured outcome exactly: the five clearable
sole-EXT functions are precisely the five that transitioned to CLEAR in E1,
and the two non-clearable ones are blocked by symbols in the excluded
classes of `docs/contracts/EXTERNAL-API-TRUST-MODEL.md` section 3.

Of the 117 EXT rows, 31 sit at bundle-covered symbols; 29 were removed in
E1 and 2 remain by design (a borrowed-return whose source argument is
untracked storage, e.g. `strchr("literal", c)`, keeps its
`unknown-pointer-return-ownership` obligation — fail-closed). The remaining
86 rows sit at deliberately excluded symbols (`__errno_location`,
`__ctype_b_loc`, `strerror`, `strtol`, `getaddrinfo`/`freeaddrinfo`,
`gai_strerror`, `strerror_r`, `fcntl`, `va_*`), which stay fail-closed
under the trust model.

## Bundle selection

The reviewed bundle set (`contracts/bundles/`) covers exactly the symbols
with measured demand that fall in accepted claim classes:
libc-memory (memcpy/memmove/memset/memcmp/memchr),
libc-string (strlen/strnlen/strchr/strncmp/strncpy/strcasecmp/strncasecmp),
libc-ctype (tolower/toupper), libc-stdio (snprintf/vsnprintf),
posix-io (close), posix-socket (connect/setsockopt/getsockopt/send/recv).
Symbols with no Hiredis demand were included only where the same claim
class was already reviewed for a sibling symbol and the cross-project
pilots showed demand (e.g. curl/libgit2); everything else waits for
measured demand plus fresh review.

## What this analysis does NOT justify

- Contracting `strtol` (pointer-to-pointer endptr) or `getaddrinfo`
  (out-owner): a partial contract would suppress the fail-closed
  pointer-output path (see trust model section 3). These need #41-style
  out-owner semantics, not bundles.
- Contracting `__errno_location`/`__ctype_b_loc`/`strerror`: static or
  thread-local returns have no accepted claim class.
- Expecting large CLEAR gains from bundles alone: 7 sole-EXT functions is
  the hard ceiling on Hiredis; the remaining Pareto is dominated by XTU
  (19 sole), STU (24 sole), and ALIAS (6 sole) blockers, which are
  separate milestones (#42, #54, and same-TU body-decision work).
