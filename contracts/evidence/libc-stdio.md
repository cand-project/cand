# Provenance: contracts/bundles/libc-stdio.yaml (cand-libc-stdio)

Reviewed formatted-output bundle.

Primary authoritative source: ISO C11 7.21.6 (printf family).
Implementation inspected: glibc `stdio-common/vfprintf-internal.c` and
musl `src/stdio/vsnprintf.c` — the destination is written during the call,
the format is read during the call, nothing is retained.

## snprintf

- ISO C11 7.21.6.5: writes at most `n-1` characters plus a terminator into
  `s` under the control of `format` ...; returns the would-be length (int).
- `int snprintf(char *restrict s, size_t n, const char *restrict format, ...)`.
- Params 0 (buffer) and 2 (format) borrow; param 1 (`n`, size_t)
  no_ownership_effect. Scalar return.
- Variadic positions are deliberately NOT covered: the verifier caps
  contract parameter lists at the declared fixed parameters, so tracked
  storage passed at a variadic position keeps its fail-closed escape
  obligation (the incident-#53 companion behavior). `%n` is not granted.

## vsnprintf

- ISO C11 7.21.6.8: as snprintf with a `va_list` argument.
- Params 0,2 borrow; param 1 no_ownership_effect. Param 3 (`va_list ap`)
  is deliberately unlisted (fail-closed unknown): va_list is an opaque
  variadic-state object with implementation-defined lifetime semantics.

Excluded (fail-closed): `sprintf` (unbounded write), `fprintf`/`printf`
(retain the FILE* stream across calls), `__isoc99_sscanf` family
(pointer-output conversions).
