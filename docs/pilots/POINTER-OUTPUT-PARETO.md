# Pointer-output addressability pareto (milestone #41 Gate A)

Status: census complete (final run v3; per-pilot JSON under the
workdirs, summarized here).

## Question

Issue #41 asks whether a narrowly specified `produces_out_owner`
contract effect can convert `unknown-call-with-pointer-output`
obligations into classified results, and requires a design only if
destination storage, success/failure behavior, nullability, and
destruction responsibility are all identifiable. This census measures
the addressable population before any implementation.

## Method

Harness: `scripts/pilots/pointer_output_pareto.py`, reusing the
cross_tu_pareto workdir machinery (cached clang JSON AST dumps, E1
report.json from the reviewed merged contract bundle). Five pilots at
the pinned commits and flags of `CROSS-TU-ADOPTION-PARETO.md`
(corrections § apply: zlib `-DHAVE_UNISTD_H`, sqlite line-preserving
`#line`-blanked lemon `parse.c` + hand-generated prerequisites, curl
configured build includes, libgit2 generated headers + refdb_reftable
exclusion). Baselines reproduce the #54 after-state exactly.

Three clang JSON dump quirks are handled explicitly and are part of
the harness contract:

1. the dump omits `line` whenever a location stays in the previously
   printed file context, so lines are recomputed from offsets against
   a line-start table;
2. `referencedDecl` nodes are abbreviated without `loc`, so full
   declaration nodes are indexed by id and resolved through the map
   (only extracted scalars are retained — node references would pin
   entire trees and exhaust memory);
3. macro-expanded expressions carry `expansionLoc`/`spellingLoc`; the
   invocation site (`expansionLoc`) is used, matching the obligation
   location. Header-declared functions are excluded from the
   function-range table (their offsets belong to other files).

## Scope split (structural finding)

`mayWritePointerStorage` accepts any address-of whose pointee may
contain a pointer AND any plain pointer whose pointee may contain a
pointer. The obligation kind therefore covers three distinct
populations, which the census separates:

- **PTR-ARG** — only plain pointer arguments: the callee may write
  pointer fields through a borrowed pointer. Not #41 territory
  (alias/write-through precision, #25/#36 family).
- **AGG-WRITE** — only `&struct`-shaped destinations: the callee
  writes caller-owned aggregate fields. Not #41 territory.
- **true out-parameters** — a `T **`-typed destination. Only the
  ADDR-LOCAL shape (destination = single dereferenceable
  function-local pointer variable) is addressable by the bounded
  rule; ADDR-LOCAL-INDIRECT rows (callback writes, e.g. zlib's
  `in(in_desc, &next)` through the `PULL` macro) have no symbol to
  bind a contract to and are excluded from the yield ceiling.

va_start/va_end/va_copy rows are counted separately (VA-BUILTIN).

## Census results

Final (zero UNMATCHED rows in every pilot; AST dumps recomputed from
offsets, macro expansions matched at invocation sites, indirect
callees separated):

| pilot | rows | PTR-ARG | AGG-WRITE | ADDR-LOCAL (direct) | ADDR-LOCAL-INDIRECT | other T** | VA |
|---|---:|---:|---:|---:|---:|---:|---:|
| hiredis | 100 | 68 | 7 | 5 | 0 | 2 | 18 |
| zlib | 103 | 71 | 9 | 0 | 15 | 6 | 2 |
| curl | 4,707 | 2,554 | 1,681 | 215 | 3 | 200 | 54 |
| libgit2 | 5,544 | 2,055 | 2,500 | 680 | 18 | 282 | 9 |
| sqlite | 4,765 | 3,803 | 661 | 48 | 7 | 206 | 40 |
| **total** | **15,219** | **8,551** | **4,858** | **948** | **43** | **696** | **123** |

Addressable callees: 240 distinct symbols (hiredis 3, zlib 0, curl 45,
libgit2 171, sqlite 22). Top: `curlx_str_number` (39 rows),
`git_commit_tree` (37), `curlx_str_single` (24), `git_reference_lookup`
(25), `curlx_str_passblanks` (20), `curl_url_get` (19).

Guard idioms on the 948 addressable rows: COND-GUARD 563,
ASSIGN-CHECKED 179, ASSIGN-UNCHECKED 76, UNCHECKED 130. Operator
classes on the 742 guarded rows: `==`/`!=` 157, implicit-boolean
(truthiness) 131, relational 454.

Refusal-predicate columns: variadic callees 0 of 948 (callee
declarations resolved for all 948 rows — a real zero); null-initialized
destinations 499/948 rows over 437 distinct (function, callee) sites.

## Interpretation

1. The obligation kind is dominated by write-through-pointer arguments
   (PTR-ARG, 56%) and aggregate writes (AGG-WRITE, 32%); the true
   out-parameter population is 1,687 rows (11%), of which 948 (6.2% of
   the kind) have the bounded rule's addressable shape.
2. 63% of addressable rows are caller-guarded, but only 288/742
   guarded rows use an operator shape the bounded rule can soundly
   recognize (`==`/`!=` and implicit boolean polarity); relational
   guards (`<`, `>`) dominate libgit2 and stay fail-closed — a
   success-conditioned write vocabulary is still essential (write:
   always contracts convert guarded and unguarded rows alike, subject
   to review of the callee's failure behavior).
3. The issue's four identifiability requirements are all satisfiable
   within the ADDR-LOCAL predicate (destination storage: AST-resolvable;
   success/failure: two dominant measurable guard idioms; nullability:
   contract field; destruction: caller-owned by construction).
4. zlib's out-params are callback writes through function pointers —
   out of scope by the direct-callee requirement, not by destination
   shape.

## Reproduction

    python3 scripts/pilots/pointer_output_pareto.py --project <p> \
        --root <pilot root> --file <f1> --file <f2> ... \
        --include ... --define ... --std gnu11 \
        --workdir <workdir with report.json> --json-out census.json

The workdir holds the E1 report.json and the cached AST dumps; the
harness is measurement-only and draws no verdicts.

## v2 measurement (milestone #41, reviewed bundle)

The implemented rule (`cand1/v1.1-draft`, ADR-0030) was measured against
the five pilots with a reviewed measurement-only bundle of 8 symbols
(never committed; derived from source review):

| pilot | symbol | contract (write / success / nullable) |
|---|---|---|
| hiredis | `getaddrinfo` | on_success / zero / false |
| hiredis | `redisGetReply` | on_success / zero / true |
| hiredis | `redisvFormatCommand` | on_success / nonzero / false |
| curl | `curl_url_get` | always / — / true |
| curl | `Curl_urldecode` | on_success / zero / false |
| libgit2 | `git_commit_tree` | on_success / zero / false |
| libgit2 | `git_commit_lookup` | on_success / zero / false |
| sqlite | `sqlite3ValueFromExpr` | always / — / true |

zlib has no addressable out-owner callee (callback writes only).

Review refusals (symbols deliberately excluded from the bundle, with the
review outcome recorded):

- `curlx_str_*` (curl, top row family): cursor advancers — the callee
  writes a borrowed interior pointer; destruction responsibility is not
  transferred, so an out-owner contract would be unsound.
- `sqlite3_prepare_v2`/`v3`: second `T **` out-param (`pzTail`) writes a
  borrowed interior pointer the v1.1-draft vocabulary cannot express; an
  accepted produce would silently drop that obligation.
- `sqlite3PagerGet`: the write is delegated through a backend function
  pointer; failure-edge write behavior is not identifiable.
- `git_reference_lookup`: failure paths are mixed (some write NULL, some
  leave the slot untouched via backend dispatch).
- `getpwuid_r` family: writes a borrowed pointer into caller storage.

Measurement protocol: per pilot, v1 = `--level=cand1` with the merged
bundle, v2 = v1 plus `--pointer-output-contracts` and the merged+PO
bundle; rows keyed by (file, line, kind).

| pilot | v1 rows | v2 rows | gone | new | ADDR-LOCAL bundle rows | converted | refused |
|---|---|---|---|---|---|---|---|
| hiredis | 861 | 864 | 2 | 5 | 5 | 1 | 4 |
| zlib | 833 | 833 | 0 | 0 | 0 | 0 | 0 |
| curl | 15424 | 15447 | 44 | 67 | 32 | 25 | 7 |
| libgit2 | 16780 | 16898 | 75 | 193 | 59 | 45 | 14 |
| sqlite | 20229 | 20233 | 8 | 12 | 5 | 4 | 1 |
| total | | | 129 | 277 | 101 | 75 | 26 |

Findings were byte-identical between v1 and v2 in every pilot: the
feature never fabricates or hides a defect verdict in the measured real
code. Converted rows are replaced by specific conservative obligations
(downstream `unrefined-out-owner-use`, tracked-pointer escapes into
unknown calls, alias-ambiguity rows), so the v2 row count grows while the
unknown-call-with-pointer-output population shrinks — the intended
trading of a coarse obligation for reviewable specific ones.

Refused-row attribution (source-verified):

- hiredis (4): loop-scoped `redisGetReply` (async.c:575);
  `redisvFormatCommand` into an uninitialized destination (async.c:974);
  `getaddrinfo` into an uninitialized destination (net.c:544);
  `getaddrinfo` into a live previously produced destination (net.c:517).
- curl (7): `Curl_urldecode` into uninitialized destinations.
- libgit2 (14): uninitialized destinations, live destinations, loop
  scope, and guard forms the refinement does not recognize.
- sqlite (1): `sqlite3ValueFromExpr` into an uninitialized destination.

An implementation defect found and fixed during the measurement, with a
fixtures-first red case:

- short-circuit chains (`if (A || B)`): the defensive terminator pass
   pre-applied the later block's element call, making the element pass
   see its own destination as live (spurious refusal; observed at
   blame_git.c:442 and attr_file.c:172-173). Fixed by pre-marking
   element calls as processed for the defensive pass
   (`pointer_output_short_circuit.c`); libgit2 conversions rose from 33
   to 45 of 59.

The five-pilot v1 zero-diff invariant was re-verified after the fix
(all pilots byte-identical to the pre-#41 baselines).
