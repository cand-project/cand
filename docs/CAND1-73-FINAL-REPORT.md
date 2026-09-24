# C&1 Milestone #73 Final Report — Borrow-idiom Modeling (ADR-0031)

**Milestone:** #73 — model the address-of-local-scalar, cursor-advance, and
pointer-output borrow idioms that cause fail-closed rows/findings on correct C
**ADR:** [ADR-0031](adr/ADR-0031-borrow-idiom-modeling.md)
**Branch:** `milestone/73-borrow-idioms` (analyzer commits `6cfbc20`, `c31b5fc`,
`781e55c`; fixture corpus `2428d25`; docs `8a1015a`+)
**Baseline:** main `baae022` (post-#74)

## 1. Summary

The #36 E2 measurement recorded six fail-closed artifacts on correct C. A
TU-level re-verification before design corrected the issue's framing: all
five findings are one mechanism (a local holding a shared borrow from a
`BorrowFromArg` summary returned while the enclosing function's summary
return effect is `Unknown`), and the three row instances are
`&scalar-param` arguments at contracted borrow parameters — the
address-of-*local* twin of which already passed silently.

ADR-0031 models three decidable forms:

| Area | Rule | Fixes |
|---|---|---|
| A | `&pointer-free` storage at a reviewed borrow-effect argument is ownership-neutral | evutil.c:3211, net.c:248+400, sqlite3.c:85990 (+30 collateral rows) |
| C | pure-integer-delta cursor advances keep the parent (`PointerRelation::Interior`); exact-base-required destruction emits `destroy-of-non-base` | the cursor-advance row families |
| R | monotone-join origin dataflow resolves returns to `BorrowFromArg(j)` from machine-verified provenance | read.c:168, buffer.c:1541/1542/1544 |

Method: fixtures-first (18-fixture paired corpus, verified fail-first), a
soundness review of the plan before implementation (three v1 defects
amended, including an unsound transfer), staged implementation with
byte-identity gates per stage, full qualification battery, and an
old-vs-new re-measurement of all 8 pilots.

## 2. Qualification battery (final tip)

- `scripts/check.sh`: green (C& 0.2.2).
- CTest 20/20 (`cand-p0-*` through `cand1-e-policy-attacks`, incl. fast fuzz).
- CVE replay: 2/2 entries within recorded classifications
  (CVE-2026-87933, CVE-2026-50219 — both BOUNDED-INCOMPLETE, ASan ground
  truth re-confirmed vulnerable-crash/fix-clean).
- #62/#64/#61 incident corpora byte-identical after every stage: the
  subscript/comma/callargs/conditional/condptr/controls compound-origin
  set, the reassigned_* set, all six conditional_join_*, p2
  `undeclared_borrow_return` (FAIL) and `direct_global_escape` (FAIL), and
  the contracts trio. Two in-development regressions were caught by these
  gates and fixed before commit — a DeclRef origin transfer reading the
  seed instead of the map entry (an exact #62 false-PASS shape, caught by
  `reassigned_borrow_origin_incomplete.c`), and a null RHS carried into
  compound-advance evaluation.

## 3. Real-world re-measurement (S6)

Setup: 8 pilots × {b0, e2, e2plus} × {old, new} binary under identical
bundles (`merged-boundaries-v2.yaml`, sha256 `fed8326c…`; e2+ =
`merged-po.yaml` + `--pointer-output-contracts`). OLD =
`/tmp/opencode/fresh-build/cand` (main `baae022`); NEW = `build/cand`
(`781e55c`). Runner `scripts/pilots/realworld_e2.py`; driver, comparator,
16 matrices, per-pilot diffs, and the 5,228-event global accounting under
`/tmp/opencode/s6/` (build products; not committed). Campaign wall time
≈ 7 min.

### 3.1 Headline

| metric | old → new |
|---|---|
| findings | **−4 unique** (read.c:168; buffer.c ×3) **+1 unique** (listpack.c:916, §4) |
| obligations | 4,538 removed / 462 added; net **down in all 24 pilot×config cells** |
| CLEAR functions | **+201 / −0** |
| TU verdicts | 10 changes (4 unique): adler32.c i→p ×3; read.c f→i ×2; buffer.c f→i ×2; listpack.c i→f ×3 |
| false PASS | **0** |
| guard violations (within-run) | OLD reproduced the 2 baseline violations (evutil 3211, amalgam 85990); NEW: 0 |

### 3.2 The six instances

| # | instance | result |
|---|---|---|
| 1 | hiredis read.c:168 B003 | removed (e2/e2+); TU fail→incomplete; `seekNewline` CLEAR |
| 2 | libevent buffer.c:1541/1542/1544 B003 | removed; TU fail→incomplete; `find_eol_char` CLEAR; companion rows @1534–1546 gone |
| 3 | redis zmalloc.c:541/568 B003 | **unchanged** (residual; zero zmalloc.c row changes, all configs) |
| 4 | sqlite-amalgam 24 `unmodelled-pointer-parameter` incl. sqlite3.c:85990 | removed; `sqlite3RealSameAsInt` CLEAR restored; guard passes |
| 5 | libevent evutil.c:3211 | removed; `evutil_set_tcp_keepalive` CLEAR restored; guard passes |
| 6 | hiredis net.c:248 (+:400) | removed (Area A) |

### 3.3 Per-pilot CLEAR gains (e2)

libgit2 +25 (23 from the `&(version)` options_init family, Area A);
sqlite-amalgam +17; sqlite +10; libevent +11; redis +9; hiredis +4; curl +3;
zlib +1. Full per-function lists in `/tmp/opencode/s6/report.md`.

### 3.4 Added-obligation accounting (risk 8 realized, contained)

All 462 additions are fail-closed rows inside already-blocked functions:
317 `unknown-call-with-tracked-pointer`, 73 `ambiguous-alias-target`, 26
`tracked-owner-overwrite`, 18 `unresolved-pointee-storage`, 13
`unknown-call-borrow-retention`, 9 `contract-body-conflict:lpSeek`, 3
`destroy-of-non-base`, 3 `borrow-unknown-parent`. The dominant pattern is
newly-tracked cursors/borrowed pointers (that the old poisoned-cursor path
silently dropped) surfacing at uncontracted call sites. Same-line
reclassifications (gzlib.c:289/294/309, fetchhead.c:248/249/255,
renameWalkWith:121482, sqlite3.c:32952) each retain a fail-closed row at or
within one line of the old one.

### 3.5 False-PASS audit

The only TU→pass is zlib `adler32.c`: all 8 removed rows are `buf++` /
`buf += 16` cursor advances and deref collateral in a pure-read checksum
loop with no free/destroy/move of any cursor in the TU; zlib's recorded
real defects (O006 gzread.c:666, gzwrite.c:720) remain findings. No
finding was removed in any TU with recorded defects. `clear_lost` = 0;
function universes identical; every added obligation sits in an
already-blocked function.

## 4. Residual debt (issue #76)

1. **redis listpack.c:916 — NEW CAND-B003** (TU incomplete→fail, all
   configs). `lpFindCbInternal` returns a cursor whose origin joins
   {param 0, param 1}: the Area R dataflow correctly refuses the
   singleton, the summary stays `Unknown`, and the flow-side backstop
   fires where the old poisoned-cursor path emitted an obligation.
   Fail-closed direction on correct code (the B003 rule working as
   specified); deciding it needs a disjunctive-return-origin summary
   vocabulary that the single-parent borrow model cannot represent.
2. **redis zmalloc.c:541/568 — unchanged**: the `ptr` chain passes
   through an uncontracted realloc-family call; requires
   lifetime-replacement modeling.
3. **`contract-body-conflict:lpSeek`** (3 rows): `combineReturn`'s
   pre-existing cross-site agreement check marks the newly-mixed
   resolved/unresolved return sites a conflict rather than `Unknown` —
   fail-closed either way; recorded for policy review.
4. **Declaration-form advance** `T *q = p + 1;` still emits
   `ambiguous-alias-target` (only assignment/compound forms modeled).
5. **`destroy-of-non-base` at sqlite3MemFree** (sqlite3.c:28405):
   correct detection of a real interior free (`p--; SQLITE_FREE(p)`, the
   size-prefix allocator) — not debt; listed because it is the new
   kind's first real-world firing.

## 5. Conclusion

All six measured instances behave exactly as designed: four correct-C
findings removed with caller-side lifetime links preserved (the UAF
detection through a returned borrow is pinned by
`borrowed_local_return_uaf_detect.c`, which fails with `CAND-B001`+`B002`
after the change), both CLEAR-loss guard violations restored, zero false
PASS, zero lost CLEAR, and one new fail-closed finding admitted as
documented residual debt with a filed follow-up. The milestone closes
issue #73.
