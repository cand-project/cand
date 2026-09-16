# Natural C application evaluation

`natural_router.c` is a 472-line, ordinary C11 packet-router batch simulator
generated for this evaluation. It has 27 functions, two pointer-returning
functions, and 22 functions accepting pointer parameters. Its packet lifecycle
uses allocation/destruction helpers, pointer-based validation and processing,
fixed-size arrays, branches, loops, and cleanup paths. The source compiles with
Clang `-std=c11 -Wall -Wextra -Werror` and runs successfully without verifier-
specific source changes or annotations.

Both analyzer versions were run on the identical file with `-std=c11`. The
P0.3 baseline was built from `e9209e70456181f135202824b59b30954104aa04`; P0.4
is the current worktree implementation.

| Measurement | P0.3 | P0.4 |
| --- | ---: | ---: |
| Functions analyzed | 27 | 27 |
| Tracked heap objects | 1 | 1 |
| Unsupported ownership operations | 116 | 53 |
| Result | INCOMPLETE | INCOMPLETE |
| Lifetime FAIL findings | 0 | 0 |

This is a 54.3% reduction in unsupported operations. P0.4 resolves wrapper
ownership and parameter effects, but does not claim this application is fully
verified: the remaining obligations are dominated by pointer-to-pointer batch
handling, unresolved pointer returns, and global/static storage. The program
was not rewritten to inline helpers or avoid normal pointer-based interfaces.

Reproduce P0.4 with:

```sh
build/cand check --format=json tests/interprocedural/natural_router.c -- -std=c11
```

The test suite keeps this fixture `INCOMPLETE` as a regression guard; the
comparison counts are exploratory acceptance evidence, not a blessed expected
PASS result.
