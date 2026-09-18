# P2.1 LLM borrowed-view repair-loop evidence

The application is `tests/interprocedural/natural_router.c`, a 680-line
ordinary C11 packet-router simulator. A packet owns its protocol records and
payload; accessors expose Ethernet, IPv4, TCP, and payload views. The source
was not flattened or rewritten for the verifier.

## Borrow surface

| metric | result |
|---|---:|
| LOC | 680 |
| borrowed-view APIs | `packet_ethernet`, `packet_ipv4`, `packet_tcp`, `packet_payload` |
| initial shared borrows | 5 |
| initial mutable borrows | 1 |
| initial result | `INCOMPLETE` (exit 3) |
| initial unsupported obligations | 61 |
| initial lifetime findings | 0 |

The initial unsupported obligations are known application-boundary semantics:
50 unmodelled pointer parameters, 7 global/static pointer-storage cases, 2
unknown calls with tracked pointers, 1 unknown pointer return, and 1
destroy-untracked-pointer case. The borrow analysis itself reports zero
unsupported borrow operations and six created relations.

## Repair loop

The agent-side loop uses a temporary copy of the application:

1. Revision A moves destruction before a later borrowed-view use. C& reports
   `CAND-B001` and `CAND-B002` with the parent object, view storage, creation
   site, death site, and access site. The deliberately invalid copy also
   exposes two transport obligations (`CAND-T002` and `CAND-T003`), rather
   than hiding them behind the lifetime error.
2. The repair moves destruction after the final view use. The repaired copy
   returns to the same honest `INCOMPLETE` result, with no lifetime finding;
   the remaining obligations are the documented unsupported application
   semantics above.

Both revisions compile and run under GCC, Clang, and ASan/UBSan. The agent
made no policy changes, verifier changes, test changes, unsafe additions, or
suppression additions. The final result is intentionally not promoted to
`PASS` while unsupported operations remain.
