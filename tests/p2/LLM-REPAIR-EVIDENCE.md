# P2 LLM repair-loop evidence

This experiment uses the existing natural packet-router application at
`tests/interprocedural/natural_router.c`. The source is intentionally not
rewritten to manufacture borrow cases.

| metric | result |
|---|---:|
| LOC | 472 |
| functions analyzed | 27 |
| explicit C& borrow relations | 0 |
| initial findings | 0 |
| initial unsupported obligations | 53 |
| repair cycles | 1 temporary-copy cycle |
| final result | `INCOMPLETE` |
| proof-policy changes | 0 |
| unsafe additions | 0 |
| suppression additions | 0 |

The initial check was deterministic: `INCOMPLETE`, exit code 3, no findings,
and 53 unsupported obligations. The obligations were primarily unmodelled
pointer parameters (44), unknown pointer-return ownership (5), global/static
pointer storage (2), tracked pointer return (1), and destruction of an
untracked pointer (1).

The repair loop ran one legitimate temporary-copy repair. It added only
source-justified lifecycle annotations for the packet constructor, packet
owner, and packet destructor. The repaired copy remained `INCOMPLETE` with 58
unsupported obligations because the application has no explicit borrowed
header/view API. No trusted contract was invented and no ordinary C alias was
promoted to a borrow.

Both the original and repaired copies compiled and ran with GCC and Clang,
including ASan/UBSan, without runtime diagnostics. The final `INCOMPLETE` is
therefore an honest unsupported-semantics result, not a repaired P2 borrow
proof. A follow-up experiment should add a natural packet-header borrowed view
and exercise B001--B004 without changing the proof policy.
