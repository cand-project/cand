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
