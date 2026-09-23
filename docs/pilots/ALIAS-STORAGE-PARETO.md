# Alias/storage evidence bar and local-alias destruction attribution (milestone #54)

> Status: Gate A measurement complete; verdict recorded. This document
> is the decision-grade evidence required by issue #54 before any
> alias/storage implementation: the evidence-bar evaluation for the
> adoption-precision track, and the pre-implementation evaluation
> (frequency, bounded design, soundness risks, corpus impact) for the
> separately tracked local-alias destruction false FAIL.

**Provenance.** Semantic source: the v0.2.2 qualified identity (`cc8e9bd`,
released as `deb692a`/tag v0.2.2) — current main at measurement time,
including ADR-0025/0026 incident repairs, the #64 compound-origin repair,
and milestone #61's ADR-0027 rule. Workdirs: the five-pilot E1 corpora
(the release-qualification configurations), `c164-*` counterfactual runs
whose verdicts were verified identical to the repo build. Harness:
`scripts/pilots/alias_storage_pareto.py` (this repository), which
reuses the `cross_tu_pareto.py` attribution machinery. Counts are H3
diagnostic observations.

## 1. Evidence-bar measurement (issue #54, condition (a))

Issue #54 requires, before alias/storage precision implementation, that
either (a) the cross-TU and external-API blocker families shrink enough
that alias/storage becomes dominant, or (b) the CVE replay corpus shows
alias/storage as a recurring real-defect path.

Sole-blocked functions by family (a function is *sole-blocked* by family
X when every obligation row in it belongs to X; alias/storage family =
`ambiguous-alias-target` + `unresolved-pointee-storage`; families per
`cross_tu_pareto.py`, with `unknown-call-with-pointer-output` (#41
scope) and `unknown-pointer-return-ownership` separated from the coarse
OTHER bucket):

| Pilot | Blocked fns | Sole-ALIAS | Sole-STU | Sole-XTU | Sole-EXT | Sole-FLOW | Sole-URET | Sole-IND | Sole-PTR-OUT |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| hiredis | 161 | **7** (4th=) | 18 | 7 | 0 | 1 | 13 | 1 | 0 |
| zlib | 108 | **18** (1st) | 6 | 0 | 1 | 13 | 1 | 2 | 0 |
| curl | 1804 | **98** (2nd) | 132 | 32 | 12 | 71 | 3 | 12 | 1 |
| libgit2 | 2648 | **71** (4th) | 284 | 125 | 38 | 138 | 47 | 10 | 2 |
| sqlite | 2269 | **145** (2nd) | 234 | 79 | 2 | 90 | 38 | 8 | 0 |

Alias/storage obligations (rows): hiredis 201, zlib 308, curl 2288,
libgit2 1817, sqlite 5520 — the second-largest obligation family overall
in every pilot.

**Reconciliation with the committed #42 census**
(`docs/pilots/CROSS-TU-ADOPTION-PARETO.md`, sole-ALIAS
7/17/100/72/153): that census ran on the #42 cross-TU workdirs, whose
per-file TU partition classifies most project callees as XTU
(sole-XTU 17/1/193/532/278 there versus 7/0/32/125/79 here); this
measurement runs on the E1 five-pilot configurations (the release
qualification corpora), where those callees are same-TU. The two
measurements agree on every decision-relevant fact: ALIAS is the #1
sole-blocker family only in zlib; the callee-summary families (STU
here, XTU under the cross-TU partition) dominate the four large pilots;
ALIAS is the second-largest obligation family everywhere. The small
ALIAS deltas (e.g. sqlite 153→145, curl 100→98) are milestone #61's
ADR-0027 clearances on the current semantics. The issue body's hiredis
figure of 6 is the older `1e1f582` attribution; the corrected #42
attribution (and this measurement) is 7 on the E0 universe of 181.

**Verdict on (a): NOT MET overall — with one explicit exception.**
Alias/storage is the #1 sole-blocker family in **zlib** (18 functions).
That is condition (a) satisfied on one pilot, and this document does
not hide it: a single pilot, bounded to 18 functions, with 13 more
sole-FLOW-blocked functions behind it, does not overturn the
cross-pilot picture — in the four large pilots the STU cascade (the
documented milestone #61 residual: H12 pointer-to-pointer, H5
unprovenanced returns, H11ind indirect calls) leads, and XTU leads
under the cross-TU partition. Dominance it is not, outside zlib.

## 2. CVE replay defect paths (issue #54, condition (b))

Whole-corpus obligation counts are alias/storage-heavy (CVE-2026-50219
libexpat: 285 `ambiguous-alias-target` + 281 `unresolved-pointee-storage`
= largest family; CVE-2026-87933 cJSON: ~205, second-largest). However,
the defect-path functions are not alias/storage-blocked:

- CVE-2026-87933, `merge_patch` (the defect function): 35 obligations
  dominated by `unknown-call-with-pointer-output` (12) and
  `unknown-call-with-tracked-pointer` (11) versus 3
  `ambiguous-alias-target`; the registry's defect-path analysis
  attributes the path through `cJSON_Delete`/`cJSON_Duplicate`
  (cross-TU callee effects).
- CVE-2026-50219: the defect mechanism is handler reentry (a callback
  path — the indirect-call family, explicitly excluded from the C&1/v1
  claim boundary); the path functions' obligations are call-family
  dominated.

**Verdict on (b): NOT MET.** Neither replay shows alias/storage as the
recurring real-defect path (2 of the 30 target CVEs replayed;
"recurring" is not establishable, and the two entries' paths are
call-family/callback dominated).

**Evidence-bar conclusion: alias/storage adoption-precision work
(ambiguity joins, pointee-storage resolution) remains deferred per the
issue's bar.** The measured dominant next target for adoption is the
callee-summary cascade (the milestone #61 residual same-TU families and
the cross-TU family), not alias/storage. The zlib exception is recorded
for the next re-measurement: if the large pilots' callee families
shrink, zlib's alias dominance is the leading indicator to re-run.

## 3. The known local-alias destruction false FAIL (verifier bug, C3)

Issue #54 separately documents a precision defect: destruction through
a local alias is misattributed as destruction of a borrowed parameter.
This section is the pre-implementation evaluation the issue requires
(frequency, bounded design, soundness risks, Hiredis/CVE-corpus
impact).

### 3.1 Reproduction and mechanism (current main)

```c
static void run(int *p) { int *q = p; free(q); }   /* FAIL: borrowed parameter cannot be destroyed */
int main(void) { int *x = malloc(sizeof(int)); run(x); return 0; }  /* legal program */
```

The summary builder's call-effect scan (`SummaryBuilder::scan`) is
syntactic: it recognizes a consuming call only when the argument
expression directly contains the parameter. `int *q = p` marks the
parameter Borrow (`markParameterFlow`); `free(q)` records nothing (the
argument is a local). The per-function flow checker, whose parameter
capability is seeded from that summary (`seedParameterState`), then
resolves `q`'s storage binding to the parameter object and reports
`CAND-O006 ownership.destroy-borrowed-parameter` — a false FAIL on a
legal program. The direct form `free(p)` produces the correct
`ParamEffect::Destroy` summary and a caller-side PASS; the two
syntactic forms of the same semantics diverge.

Fail-closed complement verified on current main: the reassigned alias
(`q = p; q = malloc(...); free(q)`) stays INCOMPLETE
(`tracked-owner-overwrite`); the genuine use-after through an alias
still FAILs (wrong locus today — the O006 masks the true
use-after-destroy).

### 3.2 Measured frequency — the primary justification

**23 of 46 total pilot findings (50%) are CAND-O006 false FAILs of this
one class.** Per-pilot: zlib 2/2 (both of zlib's findings), curl 21/42,
hiredis 0/1, libgit2 0/1, sqlite 0/0. Complete inventory (finding id,
location):

| Pilot | Locations |
|---|---|
| zlib | CAND-O006 gzread.c:666, CAND-O006 gzwrite.c:720 |
| curl | CAND-O006 curl_addrinfo.c:78, curl_threads.c:77, file.c:99, ftp.c:3955, ftp.c:4375, ftp.c:4390, imap.c:2246, imap.c:2257, mqtt.c:101, multi_ev.c:67, multi_ev.c:141, pop3.c:1662, pop3.c:1673, rtsp.c:94, rtsp.c:103, slist.c:136, smtp.c:1955, smtp.c:1965, telnet.c:177, tftp.c:900, ws.c:1370 |

This is a **C3 finding in its own right** (finding ground truth:
false-positive rate measured, not assumed): a FAIL verdict of which
half is bogus undermines the meaning of FAIL on real corpora,
independently of any adoption-rate question. That is why the false-FAIL
repair is not gated on the alias/storage dominance bar, which issue #54
attaches to the adoption-precision work ("alias/storage precision is
not the dominant adoption blocker"): reading the dominance bar as also
gating a measured-50%-false-positive verifier-bug repair would render
the issue's own, weaker evaluation clause for this defect redundant.

Shape adjudication of all 23:

- **17 direct dtor shapes** (`struct X *local = entry; ...;
  free(local)`, the curl hash-table dtor family, plus
  `curl_thread_create_thunk`): single-assignment declaration-init
  aliases of exactly one parameter, unconditional destruction — the
  bounded rule's exact target.
- **2 conditional dtors** (multi_ev.c `mev_sh_entry_dtor` free behind a
  `DEBUGBUILD`-style guard shape and `mev_pollset_dtor`, free guarded
  by `if(ps)`): resolve through the rule but join to Unknown under the
  ADR-0027 conditional gate (fail-closed), replacing the false FAIL.
- **2 loop-reassigned aliases** (curl_addrinfo.c `Curl_freeaddrinfo`,
  slist.c `curl_slist_free_all`): the alias local is reassigned in the
  loop; the bounded rule must reject these (they remain false FAILs,
  recorded as residual debt).
- **2 assignment-established aliases** (zlib `gzclose_r`/`gzclose_w`:
  `gz_statep state; ...; state = (gz_statep)file;` — the alias is
  established by a plain assignment to a pre-declared local, not by a
  declaration initializer; the bounded trace's single-assignment
  declaration-init whitelist rejects them).

### 3.3 Bounded design (ADR-0028)

A single-assignment declaration-init local-alias trace in the summary
builder, restricted to consuming call effects. When a call argument is
a local variable whose initializer is (after paren/cast stripping)
exactly one parameter reference, the local is never reassigned, its
address is never taken, it is neither volatile nor atomic nor a
parameter itself, and the parameter is never assigned in the body, the
argument is resolved to that parameter for the call's effect
computation — for `free` and for callees whose summary param effect is
Destroy or TakeOwnership only. Two surfaces: the `scan` argument
resolver, and (unchanged) the capability derivation that already seeds
the flow checker from the summary. The flow checker's
`handleFree`/`destroyBinding` paths are deliberately untouched.

### 3.4 Soundness analysis (shape table)

Over-attribution of destruction is fail-closed (it can only produce
false FAILs, never a false PASS); under-attribution is the unsound
direction and is exactly what the rule's whitelist prevents. Intended
treatment of every shape class:

| Shape | Treatment |
|---|---|
| `q = p; free(q)` (unannotated param) | resolved: param effect Destroy, O006 gone, in-flow destroy proceeds through the full direct-free path (mark Dead, double-destroy checks) |
| borrow-annotated param destroyed through alias | identical verdict to the direct-free form of the same body (the body scan already lets a consuming body override an inferred Borrow; pinned by a paired fixture so a wrong-parameter or annotation-blind resolution cannot pass silently) |
| alias destroyed, then use (`q = p; free(q); use q or p`) | FAIL via use-after-destroy at the correct locus (the O006 currently masks this) |
| alias destroyed + direct free (`q = p; free(q); free(p)`) | double-destruction FAIL |
| multi-parameter init (`q = c ? a : b`, `q = f(a, b)`) | not resolved (initializer must be exactly one parameter reference); fail-closed. `c ? p : p` (single object) is also not resolved — documented boundary, fail-closed |
| intervening reassignment of the local (`q = p; q = other; free(q)`) | not resolved (single-assignment requirement) |
| reassignment of the parameter anywhere in the body (`p = other; ...; q = p; free(q)` and the `q = p; p = NULL; free(q)` transfer idiom) | not resolved (parameter-stability requirement); the transfer idiom remains a documented false FAIL — residual debt, adjacent to #25 |
| transitive alias (`r = q; free(r)`) | not resolved (the initializer must reference a parameter, not a local); documented boundary, fail-closed |
| address-taken local (`&q` anywhere) | not resolved |
| volatile/atomic local | not resolved |
| unknown callee receiving the alias (`q = p; unknown(q)`) | not resolved (consuming-effect restriction); existing `unknown-call-with-tracked-pointer` obligation path unchanged |
| conditional destruction (`if (c) { q = p; free(q); }`) | resolved but joins to Unknown under the ADR-0027 conditional gate; in-function subsequent uses join to possible-UAF FAIL; caller-side uses stay blocked (INCOMPLETE), matching the direct-form control `conditional_join_destroy_incomplete.c` |
| loop destruction (`for (...) { q = p; free(q); }`) | same as conditional; second iteration is a double-destruction FAIL — strictly stronger than today's O006 |
| moved-param interleaving (`q = p; m = CAND_MOVE(p); free(q)`) | in-flow move/destroy bookkeeping unchanged; the object is dead after the call, so a body with no subsequent pointee use is legal (PASS) and any use after — in-function or caller-side — is a use-after-destroy FAIL (pinned by the paired `parameter_alias_move_interleaving_*` fixtures) |
| param consumed elsewhere too (`q = p; sink(p); free(q)`) | summary joins to a single Destroy/TakeOwnership per the existing merge rules |
| caller of a now-Destroy function freeing the argument after the call | double-destruction FAIL — correct: the body really does free the object |

### 3.5 Corpus impact (Hiredis/CVE, per the issue's requirement)

- **Hiredis:** 0 of its 1 finding is of this class (its finding is a
  B003 borrow-escape, untouched by the rule); the measured five-pilot
  before/after shows hiredis completely unchanged (findings 1→1,
  obligations 886→886).
- **CVE replay:** both entries report zero findings (BOUNDED-INCOMPLETE
  verdicts, findings arrays empty) — the false FAIL does not fire on
  the replay corpus, so the repair cannot change replay
  classifications; the full gate re-runs the replay regardless.
- **Pilots (measured before/after, current main + the ADR-0028 rule):**

| Pilot | Findings | Obligations | Clear |
|---|---|---|---|
| hiredis | 1 → 1 | 886 → 886 | 20 → 20 |
| zlib | 2 → 2 | 911 → 911 | 51 → 51 |
| curl | 42 → **23** | 15622 → 15622 | 345 → **347** |
| libgit2 | 1 → 1 | 17013 → 17013 | 650 → 650 |
| sqlite | 0 → 0 | 20921 → 20921 | 356 → 356 |

  Adjudication of every finding delta: all 19 removed curl findings
  are CAND-O006 of this class (17 direct dtor shapes + 2 conditional
  dtors); no new findings appear anywhere; the 2 cleared functions
  (`rtsp_easy_dtor`, `smtp_easy_dtor`) were blocked solely by the false
  FAIL; the 4 residual false FAILs are the 2 curl loop-reassigned
  shapes and the 2 zlib assignment-established shapes (section 4).

## 4. Residual debt (recorded for future milestones)

- Loop-reassigned alias destruction (curl_addrinfo/slist shapes) —
  requires flow-sensitive alias tracking, out of bounded scope.
- Assignment-established parameter aliases (the zlib `gzclose_r` /
  `gzclose_w` shapes: a pre-declared local assigned the parameter once)
  — extendable by an "assigned exactly once, from the parameter"
  variant of the whitelist; deliberately not attempted in this
  milestone (one bounded rule per Gate B).
- The `q = p; p = NULL; free(q)` parameter-transfer idiom and
  transitive aliases — adjacent to #25 (mutable-borrow last-use
  precision); documented boundaries above.
- The broader alias/storage adoption-precision families
  (`ambiguous-alias-target` obligations that are not single-assignment
  param aliases; `unresolved-pointee-storage`) — deferred per the
  evidence bar; the measured dominant next family is the callee-summary
  cascade (milestone #61 residual + cross-TU), with zlib's alias
  dominance as the leading indicator to re-run when it shrinks.
