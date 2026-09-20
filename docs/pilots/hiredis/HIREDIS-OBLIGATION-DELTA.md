# Hiredis obligation delta

This is a pilot-side comparison of the exact C& diagnostics emitted for the
Hiredis H0-H3 runs. It does not change verdict authority or interpret a
translation-unit verdict as a function-level safety claim.

## Inputs

- Hiredis: `redis/hiredis@33a12fb23531f33e3455c7ed46008c20c7ad9c78`
- C& protected-main starting point for the runs: `74adeaddbe42d3d34c1d0a26f7854871d890b7c8`
- scope: `alloc.c async.c hiredis.c net.c read.c sds.c sockcompat.c`
- source mapping: Clang 18 AST JSON, definition ranges only, with included-header
  definitions excluded

The deterministic comparison tool is
`scripts/pilots/hiredis_obligations.py`. It emits one `ROW` record for every
unsupported obligation with normalized source file, line, column, kind,
mechanism, symbol, and enclosing function. It also emits function observations.
The original JSON reports remain the measurement inputs; the pilot does not
check in generated 875 KB diagnostic dumps.

Example:

```sh
python3 scripts/pilots/hiredis_obligations.py \
  --source-root /path/to/hiredis \
  --ast-dir /path/to/ast-json \
  --mode H0=result-hiredis-h0-valid2.json \
  --mode H1=result-hiredis-h1-valid2.json \
  --mode H2=result-hiredis-h2.json \
  --mode H3=result-hiredis-h3.json
```

## Exact obligation totals

An obligation row is unique by normalized file, line, column, kind, mechanism,
symbol, and mapped function. A location is file, line, and column only.

| Mode | Unsupported obligations | Unique rows | Unique locations |
|---|---:|---:|---:|
| H0 | 1,217 | 1,217 | 1,161 |
| H1 | 1,260 | 1,260 | 1,193 |
| H2 | 1,229 | 1,229 | 1,169 |
| H3 | 1,262 | 1,262 | 1,195 |

Every H0-H3 obligation mapped to a source function. No finding was present in
these runs, so the observational function metric has no VIOLATION state.

## Exact set deltas

These are multiset deltas over the full normalized rows, not TU verdict deltas.

| Comparison | Removed | Added | Unchanged |
|---|---:|---:|---:|
| H0 → H1 | 39 | 82 | 1,178 |
| H0 → H2 | 17 | 29 | 1,200 |
| H0 → H3 | 39 | 84 | 1,178 |

### H0 → H1 removed

| Kind | Count |
|---|---:|
| `unknown-pointer-return-ownership` | 17 |
| `unknown-pointer-return-ownership:sdsempty` | 8 |
| `unknown-call-with-pointer-output` | 6 |
| `unknown-pointer-return-ownership:sdsnewlen` | 3 |
| `unknown-pointer-return-ownership:redisConnectWithOptions` | 1 |
| `unknown-call-with-tracked-pointer:sdsfree` | 1 |
| `unknown-pointer-return-ownership:redisReaderCreateWithFunctions` | 1 |
| `global-or-static-pointer-storage` | 1 |
| `unknown-call-with-tracked-pointer` | 1 |

### H0 → H1 added

| Kind | Count |
|---|---:|
| `contract-body-conflict` | 35 |
| `destroy-untracked-pointer` | 15 |
| `ambiguous-alias-target` | 13 |
| `allocation-to-untracked-storage:struct-member` | 5 |
| `unknown-call-with-tracked-pointer:sdscatlen` | 4 |
| `unresolved-pointee-storage` | 3 |
| `unknown-call-with-tracked-pointer` | 2 |
| `unknown-call-with-tracked-pointer:sdscatfmt` | 2 |
| `unknown-call-with-tracked-pointer:sdscat` | 1 |
| `unknown-call-with-tracked-pointer:sdscatvprintf` | 1 |
| `unknown-call-with-tracked-pointer:sdsMakeRoomFor` | 1 |

H0 → H2 removed 17 base `unknown-pointer-return-ownership` rows and added 21
`contract-body-conflict`, 4 `destroy-untracked-pointer`, and 4
`unmodelled-pointer-parameter` rows. H0 → H3 has the H1 removed set and adds
29 `contract-body-conflict`, 19 `destroy-untracked-pointer`, 13
`ambiguous-alias-target`, 5 struct-member storage, 4 `sdscatlen` tracked-call,
4 unmodelled-parameter, 3 unresolved-pointee, 2 unqualified tracked calls,
2 `sdscatfmt`, and one each for `sdscat`, `sdscatvprintf`, and
`sdsMakeRoomFor` tracked calls.

Therefore metadata is not semantically inert. It removes some local unknown
obligations, but the replacement obligations remain fail-closed and keep the
same translation units incomplete.

## Function-level observational metric

The tool maps obligations to Clang definition ranges. This is an observability
classification only; CLEAR is not C&1 PASS.

| Mode | Functions | CLEAR | BLOCKED | VIOLATION | TOOL-UNKNOWN |
|---|---:|---:|---:|---:|---:|
| H0 | 181 | 40 | 141 | 0 | 0 |
| H1 | 181 | 37 | 144 | 0 | 0 |
| H2 | 181 | 40 | 141 | 0 | 0 |
| H3 | 181 | 38 | 143 | 0 | 0 |

No function moved BLOCKED → CLEAR. H1 moved three previously clear functions
to BLOCKED; H3 moved two. H2 did not change the observational state counts.
This explains why local obligation removal did not produce a useful aggregate
coverage change, while also proving that the metadata affected analysis state.
