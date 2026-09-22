# Milestone #42 Gate A — Final Report

Status: final report for milestone #42 (C&1 adoption: quantify bounded
same-project cross-TU summaries). Verifier frame: exact main
`ec03ea786be36d6ce413045f413f8d358dd9c6f4`, merged reviewed E1 contract
bundle (digest `83605e67`). Evidence document:
`docs/pilots/CROSS-TU-ADOPTION-PARETO.md`. Commits `7b84324` and
`1b73f33` (PR #60).

**Conclusion (option 1): evidence insufficient for implementation —
Gate A FAILED on its usefulness criteria; Gate B not entered. Cross-TU
summaries are deferred; same-TU summary precision recommended next.**

## Report (40 items)

### Frame

1. Verifier: exact main `ec03ea786be36d6ce413045f413f8d358dd9c6f4`,
   clean tree, origin/main match, v0.2.1 current; all six tracked issues
   in expected states at start (#42/#41/#54/#39/#36/#25 OPEN, #58
   CLOSED, no open soundness incidents).
2. Measurement binary: scratch build of main + a `CAND_DUMP_SUMMARIES`
   dump hook only; verified obligation-key-identical and
   findings-identical to stock on Hiredis; never committed.
3. Contracts: merged reviewed E1 bundle (digest `83605e67`), identical
   across all runs; no contract library changes made.
4. Pilots, full project scale (not 40-file samples): hiredis `33a12fb2`
   (7 TUs), zlib `d81c2d7e` (15), curl `a40991b9` (126, lib), libgit2
   `0551dfd4` (104, src/libgit2), sqlite `30fbf300` (81, hand-generated
   prerequisites incl. lemon-built parse.c).
5. Harnesses committed: `scripts/pilots/cross_tu_pareto.py`,
   `make_amalgam.py` (with `--order preserve`), and
   `unity_counterfactual.py`.
6. Memory/disk discipline enforced after a disk-full incident (27G/27G
   from AST caches): per-TU ASTs + offsets remapping only, never
   whole-project unity AST parses; caches cleaned post-measurement.

### Corrections (A1/A2)

7. Hiredis E1 XTU corrected to **194 obligations / 62 functions / 17
   sole**; the 210/66/19 in EXTERNAL-API-BOUNDARY-PARETO.md
   misattributed 16 same-TU `__redisReaderSetError` rows (old
   `-std=c11` dumps) plus 5 IND macro rows. E0 (915) and E1 totals
   (886/4/181/23) reproduce exactly.
8. SQLite baseline corrected **392 → 366 CLEAR**: lemon `#line`
   directives into parse.y made cand's presumed-location rows
   unmappable, counting all 27 parse.c functions CLEAR; fixed by
   line-preserving `#line`-blanking in both baseline re-measurement and
   amalgam.
9. zlib baseline re-run with explicit `-std=gnu11 -DHAVE_UNISTD_H`
   reproduces the original numbers exactly (159/49/921; XTU 151/37/1).
10. libgit2 static-collision list deflated **137 → 16 names** — the
    harness's figure was inflated by macro-expansion decl
    misattribution (e.g. `git_hashset_str_*` from `GIT_HASHSET_SETUP`,
    which guard-protected headers render collision-free in a merged TU).

### A3 — per-TU baselines (family attribution, callee-resolved)

11. hiredis: 181 fns, 23 CLEAR, 4 findings, 886 obligations; XTU
    194/62/17 sole.
12. zlib: 159 fns, 49 CLEAR, 2 findings, 921 obligations; XTU 151/37/1
    sole.
13. curl: 2151 fns, 353 CLEAR, 45 findings, 15,632 obligations; XTU
    8,008/1,171/193 sole (largest family).
14. libgit2: 3298 fns, 687 CLEAR, 2 findings, 16,758 obligations; XTU
    10,692/1,843/532 sole (largest family).
15. sqlite: 2625 fns, 366 CLEAR, 2 findings, 21,190 obligations; XTU
    10,949/1,372/278 sole (largest family).
16. Sole-family audiences: sole-STU (28/12/150/356/291) matches or
    exceeds sole-XTU (17/1/193/532/278) in 4 of 5 pilots; sole-ALIAS
    7/17/100/72/153; sole-IND 4/2/36/35/41; sole-EXT 2/2/24/46/10.

### A4 — shape census (verifier's own summary dumps)

17. H (complex body) dominates everywhere: 35/42 unique callees
    (hiredis), 13/24 (zlib), 441/703 (curl), 436/1,010 (libgit2),
    711/936 (sqlite) — 40–76% of callee sites; these stay Unknown in
    the merged fixed point.
18. Remaining shapes are small or out-of-scope: A allocators (10–155),
    U outside-measured-TUs (97 curl, 251 libgit2), I interior pointers
    (1–223), C consume (1–43), plus B/F/J/L/O singletons —
    pointer-output/callback shapes are explicitly excluded from #42
    scope.

### Counterfactual method

19. Amalgamation: physical concatenation with offsets JSON; cand
    computes the true merged fixed point using only already-modeled
    same-TU effects — the exact ceiling of any #42-shaped mechanism;
    locations remapped by binary search onto per-TU function ranges.
20. Validators per project: findings identity, zero lost CLEAR,
    unmapped-row count and symmetry, gained functions present in the
    amalgam's analyzed set.
21. Disclosed measurement-only transformations: hiredis/curl none;
    zlib 11 guard-wrapped headers + gz-segments-last; sqlite 11
    guard-wrapped headers + `#line`-blanked parse.c; libgit2 2 guard
    shadows (incl. upstream `strlist.h` guard bug) + 36 in-line static
    renames in 24 files.

### Counterfactual results

22. hiredis: 23 → 24 CLEAR (**+1**, `redisAsyncSetTimeout`); findings
    4 = 4; 0 lost.
23. zlib: 49 → 50 CLEAR (**+1**, `gzclose`); findings 2 = 2; unmapped
    0; 0 lost.
24. curl: 353 → 391 CLEAR (**+38**, 1.8%); findings 45 = 45; 0 lost.
25. libgit2: 687 → 717 CLEAR (**+30**, 0.9%); findings 2 → 3; 0 lost.
26. sqlite: 366 → 408 CLEAR (**+42**, 1.6%); findings 2 → 8; 0 lost.
27. Decided summaries barely grow when merged: unique decided names
    513→514, 81→81, 524→551, 648→670, 682→710 — the gain is
    **visibility** of already-decided summaries, not new fixed-point
    decisions.
28. Merged fixed points cascade multi-hop: measured +38/+30/+42 vs
    single-pass predictions 5/14/34 (curl/libgit2/sqlite) — only the
    measured counterfactual is decision-grade.
29. Most sole-XTU-blocked functions stay blocked: capture
    6%/100%(n=1)/20%/6%/15% of sole-XTU counts — other blockers or
    H-shaped callees dominate.

### Validation & adjudication

30. sqlite unmapped rows: 3 (os_unix macro rows, symmetric with
    baseline); libgit2 unmapped 139 vs per-TU 150 — same
    macro-generated-inline class, symmetric, and **zero** unmapped rows
    fall inside any gained function.
31. All 30 libgit2 gained functions verified present in the amalgam's
    analyzed set; the 3 "lost" decided names are renamed statics
    (present under `__am` names) — no real losses.
32. sqlite +6 findings, all `p2-borrow-lifetime-v1`, all sound
    body-derived chains (schema-owned `Table`, pager interior pointers,
    lookaside allocations bounded by `db`); DETECTED-direction only, no
    PASS-direction movement, no per-TU-CLEAR regression.
33. libgit2 +1 finding (`revwalk.c:42`, pool allocation bounded by
    `walk`) — same sound class. Total: 7 lifetime findings surfaced by
    merged summaries that per-TU mode cannot see.

### A5 — alternatives comparison

34. Same-TU precision (H bodies): sole-STU audience ≥ sole-XTU in 4/5
    pilots, inside the qualified machinery, no interposition/cache
    identity/invalidation/cycle surface — the shared root cause (H) is
    addressable there.
35. #54 alias (7/17/100/72/153 sole), #41 pointer-output, #39, #25
    (IND/EXT audiences) are all smaller and were not implemented, per
    the brief.

### Gate A verdict

36. Criterion 1 (XTU recurring, ≥3 pilots): **met** — largest
    obligation family in curl/libgit2/sqlite, 22% in hiredis.
37. Criterion 2 (already-modeled effects suffice): **met** — the merged
    fixed point computes from existing effects. Criteria 3 (useful gain
    without out-of-scope shapes): **not met** — ceiling 0.6–1.8% of
    functions; criterion 4 (materially exceeds lower-risk alternatives):
    **not met**. Criterion 5 (deterministic boundary): met, not
    discriminating. **Gate A: FAIL → Gate B not entered.**
38. No stop condition was triggered (no implementation occurred, no
    false PASS possible, no non-determinism introduced); no soundness
    incident.
39. Artifacts: evidence doc `docs/pilots/CROSS-TU-ADOPTION-PARETO.md`;
    proof-plan Phase D item 2 and both Pareto docs corrected; issue #42
    commented with full evidence and **closed**; commits `7b84324` +
    `1b73f33` on **PR #60** with both required checks green
    (`cand1-v1-linux-x86_64`, `contract-and-compatibility`); v0.2.1
    claim untouched, #36/#41/#54/#39/#25 untouched and open.
40. **PR #60 requires one approving review from a write-access reviewer
    (`senolcolak`) before the repository rules allow merge — the only
    outstanding step.** Redis/hiredis GO/NO-GO: **NO-GO** for the
    cross-TU milestone on the Redis-client evidence (+1 of 181
    functions; 17 sole-XTU, 6% captured) — hiredis does not justify the
    cross-TU machinery; the recommended next milestone is same-TU
    summary precision (H bodies), where hiredis' sole-STU audience (28)
    exceeds its sole-XTU (17).
