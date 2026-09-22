# Cross-TU adoption Pareto analysis (milestone #42 Gate A)

Status: evidence base for milestone #42 (bounded same-project cross-TU
summaries). This document records the full-scale measurement of cross-TU
obligations across five real projects, the shape census of undecided
cross-TU callees, and the **amalgamation counterfactual**: the CLEAR /
findings outcome when every translation unit is physically merged into one,
so that the qualified v0.2.1 same-TU machinery computes the *true merged
fixed point*. The counterfactual is the measured upper bound for any
cross-TU summary mechanism restricted to already-modeled same-TU effects.

It supersedes the cross-TU figures in
`docs/pilots/CAND1-ADOPTION-BLOCKER-PARETO.md` and the XTU row of
`docs/pilots/EXTERNAL-API-BOUNDARY-PARETO.md` (see "Corrections" below).

## Measurement frame

- verifier: exact main `ec03ea786be36d6ce413045f413f8d358dd9c6f4`, built
  with one added measurement hook (`CAND_DUMP_SUMMARIES` env: per-function
  summary dump after the fixed point). The hooked binary was verified
  obligation-key-identical and findings-identical to the stock binary on
  Hiredis before use; the hook adds no analysis behavior.
- contracts: the merged reviewed E1 bundle (libc + libc-borrow + external
  boundary + later milestone bundles) at digest `83605e67`, identical for
  every run in this document.
- harnesses: `scripts/pilots/cross_tu_pareto.py` (family attribution,
  shape census, prediction), `scripts/pilots/make_amalgam.py` +
  `unity_counterfactual.py` / mapping (amalgamation counterfactual).

Pilots (full project scale, not the earlier 40-file samples; samples
systematically misclassify out-of-sample callees as `U`):

| project | revision | date | TUs measured | flags |
|---|---|---|---|---|
| hiredis | `33a12fb2` | 2026-09-08 | 7 | `-std=gnu11` |
| zlib | `d81c2d7e` | 2026-09-16 | 15 | `-std=gnu11 -DHAVE_UNISTD_H` |
| curl | `a40991b9` | 2026-09-19 | 126 (lib) | `-std=gnu11` |
| libgit2 | `0551dfd4` | 2026-08-15 | 104 (src/libgit2) | `-std=gnu11` |
| sqlite | `30fbf300` | 2026-09-19 | 81 (parse.c + 80 src) | `-std=gnu11`, hand-generated prerequisites |

## Corrections to earlier numbers

1. **Hiredis E1 XTU (A1)**: `EXTERNAL-API-BOUNDARY-PARETO.md` reports
   XTU = 210 obligations / 66 functions / 19 sole. Reproduced at main
   `ec03ea7` the correct E1-frame attribution is **194 / 62 / 17**. The old
   tool's `-std=c11` AST dumps missed the same-TU function
   `__redisReaderSetError` (defined in `read.c`), misattributing 16 rows
   XTU → STU; 5 further `_EL_*` macro rows belong to IND. E0 (915
   obligations) and E1 totals (886 obligations, 4 findings, 181 functions,
   23 CLEAR) reproduce exactly.
2. **SQLite baseline (this milestone)**: the first sqlite baseline reported
   392 CLEAR. lemon-generated `parse.c` emits `#line` directives into
   `parse.y`; cand reports *presumed* locations, so all 969 parse.c-TU
   obligation rows failed to map to functions and all 27 parse.c functions
   were miscounted as CLEAR. Re-measured with a line-preserving
   `#line`-blanked parse.c (identical semantics, identical line numbers):
   **366 CLEAR** (26 of 27 parse.c functions are in fact blocked; 1 is
   genuinely clear). The amalgamation uses the same blanked parse.c.
3. **zlib flags**: the original zlib workdir used `-DHAVE_UNISTD_H`
   (mirroring `configure` detection; without it `gzlib.c` does not declare
   `lseek` under gnu11). The re-run with explicit
   `-std=gnu11 -DHAVE_UNISTD_H` reproduces the original baseline exactly
   (159 functions, 49 CLEAR, 921 obligations, XTU 151/37/1).

## A3: per-TU baselines and cross-TU blocker mass

| project | functions | CLEAR | findings | obligations | XTU obl | XTU fns | XTU sole | largest family |
|---|---|---|---|---|---|---|---|---|
| hiredis | 181 | 23 | 4 | 886 | 194 | 62 | 17 | STU 215 |
| zlib | 159 | 49 | 2 | 921 | 151 | 37 | 1 | ALIAS 308 |
| curl | 2151 | 353 | 45 | 15,632 | 8,008 | 1,171 | 193 | **XTU** |
| libgit2 | 3298 | 687 | 2 | 16,758 | 10,692 | 1,843 | 532 | **XTU** |
| sqlite | 2625 | 366 | 2 | 21,190 | 10,949 | 1,372 | 278 | **XTU** |

Sole-blocked functions by family (a function blocked by several families
counts only for its sole family):

| family | hiredis | zlib | curl | libgit2 | sqlite |
|---|---|---|---|---|---|
| XTU | 17 | 1 | 193 | 532 | 278 |
| STU | 28 | 12 | 150 | 356 | 291 |
| ALIAS | 7 | 17 | 100 | 72 | 153 |
| OTHER | 1 | 10 | 53 | 74 | 77 |
| IND | 4 | 2 | 36 | 35 | 41 |
| EXT | 2 | 2 | 24 | 46 | 10 |

XTU is the largest *obligation* family in curl, libgit2 and sqlite, and
sole-XTU is the largest *sole* family there. But sole-STU is the same order
of magnitude everywhere (larger than XTU in hiredis, zlib, libgit2,
sqlite): undecided callee bodies are the dominant phenomenon, and the TU
boundary is only one of the reasons a callee summary is undecided.

## A4: shape census of XTU callees

Census over the verifier's own per-TU summary dumps (unique cross-TU
callees, dominant shapes):

| project | unique XTU callees | H (complex body) | A (allocator) | U (outside TUs) | I (interior ptr) | C (consume) | others |
|---|---|---|---|---|---|---|---|
| hiredis | 42 | 35 | – | – | 4 | 1 | F2 |
| zlib | 24 | 13 | 10 | – | 1 | – | – |
| curl | 703 | 441 | 102 | 97 | 44 | 11 | B7 J1 |
| libgit2 | 1,010 | 436 | 57 | 251 | 223 | 43 | – |
| sqlite | 936 | 711 | 155 | 4 | 28 | 30 | L6 O1 J1 |

The dominant shape is **H** (callee body too complex for the current
same-TU summary lattice: locals, conditional effects, struct-field
returns) at 40–76% of cross-TU callee *sites*. These callees remain
`Unknown` **even in the merged fixed point** — the TU boundary is not what
blocks them.

## The amalgamation counterfactual (measured merged fixed point)

Method: concatenate every measured TU into one physical file
(`make_amalgam.py`, `/* ==== BEGIN <file> ==== */` markers + offsets JSON),
compile-check, run the same qualified v0.2.1 cand with the same contracts,
and remap every obligation/finding from amalgam coordinates back to
original files (binary search over offsets) onto the per-TU function
ranges. Validators: findings identity (no lost findings), zero lost CLEAR,
unmapped rows = 0. The amalgam is a *build product*, never committed.

Because cand's per-TU summaries are computed by a bounded fixed point
(`SummaryBuilder`), the amalgam run yields exactly what a perfect, sound,
project-wide cross-TU summary store would compute for the
already-modeled effect set — it is the ceiling for any #42-shaped
mechanism that does not add new effect kinds.

| project | per-TU CLEAR | amalgam CLEAR | Δ | % of functions | findings per-TU → amalgam | lost CLEAR |
|---|---|---|---|---|---|---|
| hiredis | 23 | 24 | **+1** | 0.6% | 4 = 4 | 0 |
| zlib | 49 | 50 | **+1** | 0.6% | 2 = 2 | 0 |
| curl | 353 | 391 | **+38** | 1.8% | 45 = 45 | 0 |
| sqlite | 366 | 408 | **+42** | 1.6% | 2 → 8 (see below) | 0 |
| libgit2 | 687 | 717 | **+30** | 0.9% | 2 → 3 (see below) | 0 |

Amalgam sizes and cost: hiredis 5,517 lines; zlib 9,738; libgit2 85,934
(104 files, 0 diagnostics, 2.0 s, 219 MiB RSS); curl 100,755; sqlite
173,251 (1.95 s, 220 MiB RSS).

Why the ceiling is small despite XTU being the largest family:

- The merged fixed point decides barely more *summaries* than per-TU mode:
  decided summary names per-TU → merged: hiredis 513 → 514, zlib 81 → 81,
  curl 524 → 551, sqlite 682 → 710, libgit2 648 → 670. The gain is
  overwhelmingly **visibility** (per-TU-decided summaries becoming callable
  across TUs), not new fixed-point decisions; H-shaped callee bodies stay
  Unknown.
- Most sole-XTU-blocked functions stay blocked anyway: capture is
  6% (hiredis), 100% (zlib, n=1), 20% (curl), 15% (sqlite), 6% (libgit2)
  of sole-XTU counts — the typical sole-XTU function also has other
  blockers once its XTU callees resolve, or its callees are H-shaped.
- Single-pass predictions underestimate/overestimate: predicted-clear
  (strict) was 4/0/5/14/34 vs measured +1/+1/+38/+30/+42. Merged fixed
  points cascade multi-hop (a newly-visible caller summary unblocks its
  own callers), which single-pass prediction cannot see. Only the
  measured counterfactual is decision-grade.

### SQLite findings delta (DETECTED-direction gain)

The sqlite amalgam produces 6 additional findings, all rule
`p2-borrow-lifetime-v1` (borrow escapes through an undeclared return):

| site | mechanism (merged summary chain) |
|---|---|
| `alter.c:2820` | returns `sqlite3LocateTableItem(...)` → `borrow_from_arg` (schema-owned `Table`) |
| `delete.c:45` | same callee chain |
| `btree.c:2348` | returns `sqlite3PagerGetExtra(pDbPage)` → interior pointer into the page |
| `fkey.c:496` | `sqlite3ExprAddCollateString` → lookaside allocation, lifetime bounded by `db` |
| `fkey.c:516` | `sqlite3Expr(db,...)` → `sqlite3DbMallocRawNN` reads the lookaside free list from `db` → `borrow_from_arg(0)` |
| `trigger.c:439` | `sqlite3DbSpanDup` → db-owned allocation |

All six are sound body-derived propagations of the existing (qualified)
borrow-lifetime rule to functions whose callees were previously
cross-TU-unknown; each reflects a real lifetime bound (schema/page/db
handle). No finding was lost, no per-TU-CLEAR function regressed, and no
finding moved any function toward PASS. This is the
BOUNDED-INCOMPLETE → DETECTED direction the #42 requirements permit.

### libgit2 findings delta (DETECTED-direction gain)

One additional finding, `p2-borrow-lifetime-v1` at `revwalk.c:42`:
`git_commit_list_alloc_node(walk)` allocates from the revwalk's internal
commit pool, so the returned node's lifetime is bounded by `walk` —
returned undeclared. Same sound body-derived class (pool allocation →
`borrow_from_arg`). Both per-TU findings are preserved byte-identically;
0 lost.

### libgit2 mapping validators

Per-TU unmapped rows 150/16,758 (0.9%) vs amalgam 139/12,813 (1.1%) —
the same class on both sides (obligations inside file-scope
macro-generated inline functions, e.g. `GIT_HASHMAP_STR_SETUP` at
`attrcache.c:18`, whose scratch-space locations fall outside function
ranges in both modes; treatment is symmetric). Zero unmapped amalgam rows
fall inside any gained function's range, and all 30 gained functions are
present in the amalgam's analyzed set — the +30 is not a mapping
artifact.

## Disclosed measurement-only transformations

- **hiredis**: none (clean concatenation, 0 diagnostics).
- **curl**: none (100,755 lines; 112 benign redefinition warnings).
- **zlib**: 11 outer-guard-wrapped headers in a shadow include dir
  (zlib headers are unguarded by design); gz* segments placed last
  (`--order preserve`) so `gzguts.h` macros (`COPY`, `GZIP`) do not
  shadow `inflate.h`'s enum; `-DHAVE_UNISTD_H` (see Corrections).
- **sqlite**: 11 outer-guard-wrapped headers; line-preserving `#line`
  blanking of lemon's `parse.c` in both the re-measured baseline and the
  amalgam (see Corrections); amalgam rebuilt from the same blanked file.
- **libgit2**: outer-guard shadows for `config_list.h` and `strlist.h`
  (upstream bug: `strlist.h` reuses the guard macro
  `INCLUDE_runtime_h__`, eliding `runtime.h` on second inclusion);
  per-file rename of colliding file-scope symbols to `<name>__am<k>`
  inside the amalgam segments only: **16 colliding names, 36 renames in
  24 files** (static functions such as `interesting`, `create_branch`,
  `write_tree`; static variables; file-scope struct tags such as
  `object_entry_cb_state`). Renames are in-line (line numbers preserved),
  so location-based remapping is unaffected. Note: the earlier
  `cross_tu_pareto` "static_name_collisions" figure of 137 names for
  libgit2 was inflated by the same macro-expansion misattribution fixed
  for this measurement (header-macro-generated decls, e.g.
  `git_hashset_str_*` from `GIT_HASHSET_SETUP`, are attributed to their
  guard-protected headers and do not collide in a merged TU).

## A5: milestone comparison on the same measurements

Sole-blocked functions by candidate milestone (the addressable audience
per project):

| candidate | hiredis | zlib | curl | libgit2 | sqlite | risk surface |
|---|---|---|---|---|---|---|
| #42 cross-TU summaries (this doc) | 17 | 1 | 193 | 532 | 278 | new interposition, caching, invalidation, cycle handling |
| same-TU summary precision (H bodies) | 28 | 12 | 150 | 356 | 291 | no new trust surface |
| #54 alias precision | 7 | 17 | 100 | 72 | 153 | lattice widening |
| #39 / #25 / #41 | smaller (IND 4/2/36/35/41; EXT 2/2/24/46/10; pointer-output rows) | | | | | |

Measured outcome ceilings:

- #42's ceiling (amalgam counterfactual): **+1/+1/+38/+30/+42 CLEAR**
  (0.6–1.8% of functions) plus a modest DETECTED-direction findings gain
  (6 findings on sqlite, 1 on libgit2, 0 elsewhere).
- The same-TU precision audience (sole-STU) is comparable or larger in
  every project, sits *inside* the already-qualified per-TU machinery
  (no interposition, no cache identity, no invalidation, no cycles), and
  its blocker mass is the same phenomenon (H bodies) that caps #42's
  ceiling.

## Gate A assessment

Applying the acceptance criteria from the #42 brief to the measured
evidence:

1. *XTU recurring in ≥3 pilots as a significant blocker*: **met** on
   obligations — XTU is the largest obligation family in curl, libgit2
   and sqlite, and 194/886 (22%) in hiredis.
2. *Significant portion of XTU effects already modeled same-TU*:
   **met** — the amalgam counterfactual computes the merged fixed point
   entirely from already-modeled effects with zero new machinery.
3. *Useful gain without callbacks/pointer-output/realloc/alias/dispatch*:
   **not met** — the measured ceiling of perfect cross-TU visibility is
   +1/+1/+38/+30/+42 CLEAR (0.6–1.8% of functions). The shapes that
   would deliver more are H (complex bodies, 40–76% of callee sites,
   requiring new same-TU effect modeling) and the out-of-scope B/U/F/J
   shapes (pointer-output, callbacks).
4. *Improvement materially exceeds lower-risk alternatives*: **not
   met** — sole-STU audiences (28/12/150/356/291) match or exceed
   sole-XTU (17/1/193/532/278) in four of five pilots, while requiring
   no new trust surface (no interposition model, cache identity,
   invalidation, or cycle handling); the amalgam evidence shows the
   H-body phenomenon — the shared root cause — dominates both, and it
   is addressable inside the qualified per-TU machinery.
5. *Deterministic boundary tied to exact source*: met by design (same
   binary, contracts, flags for both sides of every comparison).

**Gate A verdict: FAIL on criteria 3 and 4.** Cross-TU summaries are
sound to compute and do produce real DETECTED-direction gains (7
lifetime findings across sqlite and libgit2 that per-TU mode cannot
see), but the CLEAR-direction ceiling measured on the merged fixed point
is 0.6–1.8% of functions across five structurally different projects,
does not exceed the same-TU precision alternative, and the mechanism
would carry the full interposition/caching/invalidation burden that
SPEC-0010 currently excludes. Per the milestone brief, Gate B is not
entered; the evidence recommends prioritizing same-TU summary precision
(sole-STU audience, H bodies) as the next milestone instead.

Methodological byproducts retained for future milestones: the
amalgamation counterfactual harness (committed: `make_amalgam.py`,
`unity_counterfactual.py`, `cross_tu_pareto.py`), the corrected
misattribution classes (`#line`-presumed locations; macro-expansion
decl attribution), and the measured cascade effect (merged fixed points
clear multi-hop, so single-pass predictions are not decision-grade).
