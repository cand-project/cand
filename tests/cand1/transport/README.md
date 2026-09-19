# C&1-B transport corpus

The shell fixture checks representative existing P2 transport cases and the
deterministic generator checks the strict invariant:

```text
tracked pointer transport + cand --level=cand1 -> not PASS
```

`adversarial.py` generates 250 implementation cases and 75 structurally
different independent cases across aggregate, array, union, byte-copy, cast,
provenance, interior-pointer, global/static, callback, out-parameter,
varargs, atomic, non-local-control-flow, and `realloc` boundaries. A case is
accepted only when the report has a semantic finding or an unsupported
obligation; an empty report is treated as metadata loss.

`benchmark.py` measures 1k and 10k aggregate-heavy functions. It requires
non-PASS and records wall time and transport-event counts.
