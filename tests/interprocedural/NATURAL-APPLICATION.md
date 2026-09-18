# Natural C application evaluation

`natural_router.c` is a 680-line C11 packet-router simulator. It models a
packet owner and borrowed Ethernet, IPv4, TCP, and payload views. The
application includes allocation, parsing, routing, statistics, queues,
branches, loops, cleanup paths, and protocol-view inspection.

The source compiles with GCC and Clang using `-std=c11 -Wall -Wextra -Werror`
and runs cleanly under ASan/UBSan. The C& result is intentionally
`INCOMPLETE`: the supported borrow relations are analyzed, while ordinary
pointer-parameter, callback/storage, and unknown-return edges remain
fail-closed.

Current P2.1 evidence:

| measurement | result |
|---|---:|
| LOC | 680 |
| shared borrows created | 5 |
| mutable borrows created | 1 |
| borrow operations unsupported | 0 |
| total unsupported obligations | 61 |
| result | `INCOMPLETE` |

Reproduce with:

```sh
build/cand check --format=json tests/interprocedural/natural_router.c -- -std=c11 -Iinclude
```

The companion repair-loop record is
`tests/p2/LLM-REPAIR-EVIDENCE.md`.
