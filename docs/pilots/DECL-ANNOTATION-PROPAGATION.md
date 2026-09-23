# Declaration-site annotation propagation (Issue #39)

- **Status:** milestone #39 Gate B complete; ADR-0029
- **Branch:** `milestone/39-decl-annotation`
- **C& starting main:** `4e35928`
- **Census harness:** `scripts/pilots/decl_annotation_pareto.py` (committed
  `4c80611`); census JSONs measured 2026-09-22
- **Hiredis:** `redis/hiredis@33a12fb23531f33e3455c7ed46008c20c7ad9c78`
  (same pin as the pilot); 17-site annotation patch
  `docs/pilots/hiredis/patches/0001-reviewed-cand1-ownership-metadata.patch`
- **Manifests:** `docs/pilots/hiredis/fixtures/h2-fixture-annotation-review.yaml`
  (fixture level) and `docs/pilots/hiredis/contracts/h2-annotation-review.yaml`
  (full project, 17 symbols)

## 1. Gate A: addressable-surface census

`decl_annotation_pareto.py` classifies every `unknown-call-*` /
`unknown-pointer-return-ownership` obligation row from the current five-pilot
baseline reports by whether its callee is a body-less declaration that is
defined in another pilot TU (XTU-ELIGIBLE — the surface #39 can address),
guarded (`**`/K&R/realloc/C++ shapes), same-TU, indirect, or system/other.

| Pilot | Annotation-relevant rows | XTU-ELIGIBLE rows | Distinct XTU symbols |
|---|---:|---:|---:|
| hiredis | 560 | 200 | 38 |
| zlib | 337 | 145 | 23 |
| curl | 12,036 | 5,879 | 560 |
| libgit2 | 14,405 | 4,641 | 533 |
| sqlite | 13,860 | 10,671 | 891 |
| **total** | **41,198** | **21,536** | **2,045** |

SQLite is the leading case: 77% of its annotation-relevant rows are
XTU-ELIGIBLE. The evidence bar for a bounded propagation rule is met.

## 2. Gate A: baseline H-matrix (main `4e35928`)

Nine hiredis fixtures × H0/H1/H2/H3 (H2 = annotations, manifest-less, since
no manifest existed before #39):

```text
01-owned-return-destructor               H0=incomplete (1)     H1=pass               H2=incomplete (1)     H3=pass
02-borrowed-parameter                    H0=pass               H1=pass               H2=pass               H3=pass
03-consumed-parameter                    H0=incomplete (1)     H1=pass               H2=incomplete (1)     H3=pass
04-pointer-to-pointer-output             H0=incomplete (1)     H1=incomplete (1)     H2=incomplete (1)     H3=incomplete (1)
05-cross-tu-owned-return                 H0=incomplete (1)     H1=pass               H2=incomplete (1)     H3=pass
06-matching-body-contract                H0=pass               H1=pass               H2=pass               H3=pass
07-conflicting-body-contract             H0=pass               H1=incomplete (1)     H2=incomplete (2)     H3=incomplete (2)
08-realloc-boundary                      H0=incomplete (1)     H1=incomplete (1)     H2=incomplete (1)     H3=incomplete (1)
09-callback-retention                    H0=incomplete (3)     H1=incomplete (3)     H2=incomplete (3)     H3=incomplete (3)
```

The #39 defect is pinned: 01/03/05 PASS under H1 (contracts) but INCOMPLETE
under H2 (annotations).

**Baseline drift note (fixture 07):** the historical integration table
recorded 1 obligation for H2/H3; the current main records 2
(`tracked-pointer-return` line 5 + `contract-body-conflict` line 9). The
verdict is unchanged (INCOMPLETE, fail-closed); the extra row is drift from
later milestones and is documented here, not repaired.

## 3. Post-fix H-matrix

H2 is now *annotations + review manifest*; **H2′** (manifest-less) is the
control that preserves the recorded attack surface:

```text
                                        H0                  H1                  H2'                  H2                   H3
01-owned-return-destructor               incomplete(1)       pass(0)             incomplete(1)        pass(0)              pass(0)
02-borrowed-parameter                    pass(0)             pass(0)             pass(0)              pass(0)              pass(0)
03-consumed-parameter                    incomplete(1)       pass(0)             incomplete(1)        pass(0)              pass(0)
04-pointer-to-pointer-output             incomplete(1)       incomplete(1)       incomplete(1)        incomplete(1)        incomplete(1)
05-cross-tu-owned-return                 incomplete(1)       pass(0)             incomplete(1)        pass(0)              pass(0)
06-matching-body-contract                pass(0)             pass(0)             pass(0)              pass(0)              pass(0)
07-conflicting-body-contract             pass(0)             incomplete(1)       incomplete(2)        incomplete(2)        incomplete(2)
08-realloc-boundary                      incomplete(1)       incomplete(1)       incomplete(1)        incomplete(1)        incomplete(1)
09-callback-retention                    incomplete(3)       incomplete(3)       incomplete(3)        incomplete(3)        incomplete(3)
```

- The defect is repaired: 01/03/05 PASS under H2 exactly as under H1.
- The manifest-less control H2′ keeps every verdict INCOMPLETE with the new
  fail-closed kind, e.g. `unreviewed-declaration-annotation:fixture_owned`
  (previously `unknown-pointer-return-ownership:fixture_owned`). The
  candidate-only external declaration annotation attack remains INCOMPLETE,
  preserving the control recorded in
  `docs/pilots/hiredis/HIREDIS-CONTRACT-RECONCILIATION.md` verbatim.
- All scope guards (`**` 04, body conflict 07, realloc 08, callback
  retention 09) are unchanged.

## 4. Fixture suite (Gate B)

28 new checks in `tests/interprocedural/run.sh` over 20 fixtures and 11
manifests, plus a malformed-manifest exit-2 check. Post-fix verdicts:

| Group | Fixture | Manifest | Verdict |
|---|---|---|---|
| owned return | annotation_review_owned_return | none | incomplete (`unreviewed-declaration-annotation`) |
| | | matching | pass |
| | | mismatching facts | incomplete (`unreviewed-declaration-annotation`) |
| borrowed return | borrow_return_safe / _escape | matching | pass / fail |
| destroy param | destroy_param_safe / _uaf | matching | pass / fail |
| takes param | takes_param_safe / _double_use | matching | pass / incomplete |
| borrow param | borrow_param_safe | matching | pass |
| callback | callback_borrow | matching | pass (H1 parity) |
| contract merge | contract_agree / _disagree | contract + manifest | pass / incomplete (`annotation/contract mismatch`) |
| redecl conflict | redecl_conflict_return / _param | matching | incomplete (`conflicting-declaration-annotation`) |
| guards | ptr_ptr_param / ptr_ptr_return | matching | incomplete (today's kinds) |
| guards | knr / realloc | matching | incomplete (today's kinds) |
| edge | malformed_borrow_index | none | incomplete |
| edge | indirect | none | incomplete |
| malformed manifest | any | malformed | exit 2 (tool error) |

### Bring-up corrections (recorded, not hidden)

- `annotation_review_owned_return_mismatch.yaml` and the
  `contract_disagree` companion contract originally recorded `from_param: 0`
  for zero-parameter declarations — malformed against the (correct)
  shape validation; rewritten as valid-but-different fact sets.
- The borrow-return fixtures originally passed a stack address as the
  borrowed-from argument; the borrow-from-arg binding requires a tracked
  argument (matching the contract twin `trusted_external_view_*`), so the
  fixtures now use a heap owner and annotate the borrowed-from parameter
  (`CAND_BORROW`), which the manifest records.
- `takes_param_double_use` (consume-then-use) is INCOMPLETE in both the
  annotation and contract modes (`access-unknown-ownership-state`); pinned
  to the identical contract-twin verdict (H1 parity) rather than FAIL.
- Same-TU statics in fixture 09 dominate its INCOMPLETE (`global-or-static-
  pointer-storage`), not the callback; no callback-specific guard is needed
  beyond H1 parity.
- K&R note: redeclaration annotations placed after a same-TU definition are
  dropped by Clang (0 attrs on the merged decl) — a language boundary,
  documented, not a C& defect. Conflicting annotations across redeclarations
  both arrive `Inherited` on the merged decl (verified via `clang-18
  -ast-dump`).

## 5. Five-pilot zero-annotation invariant

The five pilots (hiredis, zlib, curl, libgit2, sqlite; same file sets and
flags as the #54 after-measurement) were re-run with the #39 binary.
Obligations and findings are byte-identical to the #54 after-state
(`a54-*` workdirs) in every pilot:

| Pilot | Obligations | Findings |
|---|---|---|
| hiredis | 886 → 886, identical | 1 → 1, identical |
| zlib | 911 → 911, identical | 2 → 2, identical |
| curl | 15,622 → 15,622, identical | 23 → 23, identical |
| libgit2 | 17,013 → 17,013, identical | 1 → 1, identical |
| sqlite | 20,921 → 20,921, identical | 0 → 0, identical |

(Measurement note: the first invariant attempt compared against the stale
pre-#54 `c164-*` reports, whose curl findings count (42) was the #54
*before* value; the authoritative comparison target is the #54 after-state,
shown above. All pilots have zero annotations, so seeding sets are empty.)

## 6. Full-project Hiredis replay

The seven-TU pilot scope (`alloc.c async.c hiredis.c net.c read.c sds.c
sockcompat.c`, `-I. -std=gnu11 -DCAND_ANALYSIS`), patched tree with the
17-site annotation patch:

| Mode | Obligations | Findings |
|---|---:|---:|
| H0 zero metadata | 1,031 | 0 |
| H1 contracts (17-symbol `h1-reviewed.yaml`) | 1,060 | 0 |
| H2′ annotations, manifest-less (control) | 1,037 (29 `unreviewed-declaration-annotation`) | 0 |
| H2 annotations + manifest (`h2-annotation-review.yaml`) | 1,052 | 0 |
| H3 contracts + annotations + manifest | 1,058 | 0 |

H2 vs H1 differs by 8 net obligations; the per-row diff is fully
attributed:

- 13 same-line pairs where H1 reports `contract-body-conflict` and H2
  reports a body-derived obligation (`destroy-untracked-pointer` or
  `unknown-call-with-tracked-pointer`): calls to functions defined in the
  same TU (redisFree/freeReplyObject/sdsnew… in hiredis.c/read.c/sds.c).
  Contracts apply in the defining TU and conflict with the body summary;
  annotations never override visible bodies (body precedence), so the same
  calls fall back to the body's own (imprecise, fail-closed) summary. Both
  modes stay INCOMPLETE; H2 reports fewer conflicts for the same reviewed
  facts.
- 1 row resolved only in H2 (`async.c:495`, `sdsnewlen`): the reviewed
  annotation set is silent on `sdsnewlen`'s parameters — un-annotated
  parameters mean `no_ownership_effect` (annotation-language semantics) —
  while the contract bundle leaves the parameters unlisted, which means
  `unknown` (contract-format semantics). The difference is the reviewed
  artifacts' completeness, not the mechanism (ADR-0029 decision 4).

## 7. Nine-mutation corpus (false-PASS control)

Reconstructed per the disposable recipe in
`HIREDIS-INTEGRATION-RESULTS.md` (reply/reader/context/SDS/command temporal
defects; the reply objects are constructed directly rather than through a
live server, which does not change the temporal defect class). Each
mutation compiled against the patched ASan/UBSan hiredis build and executed
independently; C& verdicts under H2 (annotations + manifest) and H2′:

| Mutation | ASan/UBSan | C& H2 | C& H2′ |
|---|---|---|---|
| reply use-after-free | confirmed (exit 1) | fail | incomplete |
| reply double-free | confirmed (exit 1) | fail | incomplete |
| reader use-after-free | confirmed (exit 1) | fail | incomplete |
| reader double-free | confirmed (exit 1) | fail | incomplete |
| sds use-after-free | confirmed (exit 1) | fail | incomplete |
| sds double-free | confirmed (exit 1) | fail | incomplete |
| context use-after-free | confirmed (exit 1) | fail | incomplete |
| context double-free | confirmed (exit 1) | fail | incomplete |
| command use-after-free | confirmed (exit 1) | fail | incomplete |

**Confirmed in-scope temporal defect + authoritative PASS: 0.** The
historical gate recorded 9/9 INCOMPLETE; with reviewed annotations +
manifest the same nine defects are now detected FAILs — the propagation
converts previously-undetected (fail-closed) defects into authoritative
failures without any new trust in candidate-authored input.

## 8. ABI/layout control

Pristine and patched trees built with identical flags (Release,
clang-18):

```text
sizeof(redisReply)=64 sizeof(redisContext)=272 sizeof(redisReader)=224   (both builds)
exported-symbol-list sha256 (both builds):
9b83256f388f645a009535739338c6ff830017684b61dea5657b7ec7a9463b1b
```

The absolute SHA differs from the historical control
(`76047a71…`) because that measurement used the sanitizer build; the
control property — pristine and patched identical under identical flags —
holds. No struct layout, function ABI, or exported symbol changed.

## 9. Fuzz extension

Seven `EXTERN_*` mutation operators (ADR-0029 boundary) in
`tests/fuzz/mutate.py`, exercised against the strict generated-profile
workspace with a FIXED review manifest pinned at the baseline policy:

- `EXTERN_OWNED_RETURN_{SAFE,UAF,DOUBLE_FREE}`,
  `EXTERN_BORROW_LIFETIME_{SAFE,UAF}`: verdicts PASS/FAIL/FAIL as
  expected; the three violations are ASan-confirmed through real helper
  definitions compiled into the ASan binary only.
- `EXTERN_UNREVIEWED_NO_MANIFEST`, `EXTERN_FACT_MISMATCH`: INCOMPLETE —
  the fixed manifest cannot be satisfied by absent or contradicting
  declaration facts, and no generated case can change trusted facts.

Fast campaign (seed 12345, 1,000 cases): 0 false PASS, 0 false positive,
48/48 mutation operators correct, all gates green.

## 10. Agent path

`tests/agent/run.sh` gains: unpinned annotation-review manifest BLOCKED
(fail-policy, `annotation-review-set-substitution`), pinned manifest PASS
with `annotation_reviews` evidence round-trip, and post-attestation
manifest mutation DETECTED (`stale`). The evidence schema
(`contracts/schema/cand-evidence.schema.json`) gains the optional
`annotation_reviews` array (same item shape as `contracts`).

## 11. Qualification summary

- `scripts/check.sh`: all repository checks pass (C& 0.2.2 identity
  unchanged — the v0.2.2 claim surface is not amended by this milestone;
  the trusted-input list amendment is documented in `SAFETY_CLAIMS.md`).
- CTest: 20/20.
- CVE replay: 2/2 (via CTest).
- Five-pilot invariant: byte-identical (§5).
- Mutation corpus gate: 0 false PASS (§7).
- ABI/layout: unchanged (§8).

## 12. Reviewer flags (senolcolak)

- **D1:** the manifest exists at all (versus refusing declaration-site
  propagation).
- **D2:** manifest facts must equal annotation facts exactly.
- **D3:** un-annotated declaration parameters mean `no_ownership_effect`,
  not `unknown` (§6 async.c:495 is the observable consequence).
- **D4:** annotation/contract disagreement fails closed rather than
  preferring either source.
- **D5:** the manifest is pinned through the existing `contracts.trusted`
  pin list rather than a new policy section.
- **D6:** the `realloc` name-based guard mirrors the contract loader.
