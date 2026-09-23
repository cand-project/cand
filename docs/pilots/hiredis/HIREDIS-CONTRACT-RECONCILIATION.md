# Hiredis contract/body reconciliation follow-up

This is the bounded #43 follow-up after PR #44. It changes only the trusted
contract representation/reconciliation path and its diagnostics. It does not
add cross-TU summaries, pointer-output ownership, `realloc` semantics,
callback retention, or declaration-annotation authority.

## Inputs

- C& protected-main base after PR #44: `0050672e1db63c4ab2b2111607ed32f3b8c7b273`
- verifier implementation used for the final rerun: `2592b9ab54e6879fc6e1c583aff6b43266950653`
- Hiredis: `redis/hiredis@33a12fb23531f33e3455c7ed46008c20c7ad9c78`
- Hiredis scope: `alloc.c async.c hiredis.c net.c read.c sds.c sockcompat.c`
- qualified profile: Ubuntu 24.04 / x86_64 / Clang 18.1.3 / C11
- trusted bundle: `docs/pilots/hiredis/contracts/h1-reviewed.yaml`, 17 symbols

The historical H0 baseline remains immutable: 5,509 analyzed LOC, 370 PASS,
0 FAIL, 5,139 INCOMPLETE, and 0 confirmed false PASS.

## Trust boundary

Verified same-TU body facts, explicitly selected trusted external contracts,
and built-in models are proof inputs. Candidate/source annotations on an
external declaration are not independent proof authority. They may be used as
consistency expectations or ergonomics hints, but an unknown external effect
remains INCOMPLETE without a selected trusted contract or builtin model.
Suggested/inferred contracts are likewise not proof authority.

Since milestone #39 (ADR-0029), a declaration's annotations seed a summary
only when a separately reviewed annotation-review manifest
(`cand.annotation-review/v1`, policy-pinned like a contract bundle) records
exactly the same facts. The candidate-only control below is unchanged: an
annotation without its reviewed manifest stays INCOMPLETE.

For a visible body, a trusted contract constrains only fields it explicitly
states. Omission means no claim. An explicit `no_ownership_effect` is a known
fact and is checked against the body. Contradictions remain INCOMPLETE.

## Representation and compatibility

The old loader represented a contract directly as `FunctionSummary`, resized
omitted parameters with `ParamEffect::None`, and compared the whole summary to
the visible body. That made omitted fields indistinguishable from explicit
`no_ownership_effect` and created false contract/body conflicts.

The loader now parses a separate partial `ContractSummary`:

- optional return effect and borrow origin;
- optional per-parameter effects;
- an omitted field is UNSPECIFIED/no claim;
- an explicit `no_ownership_effect` is `ParamEffect::None` with presence;
- an external summary materializes omitted effects as `Unknown`, preserving
  fail-closed behavior at bodyless declarations.

Reconciliation is compatibility checking, not override:

| Contract fact | Body fact | Result |
|---|---|---|
| omitted | any body fact | body fact retained |
| explicit X | X | compatible; body retained |
| explicit X | incompatible Y | `contract-body-conflict` / INCOMPLETE |
| explicit X | unknown or conditional | conflict / INCOMPLETE |
| explicit no-effect | ownership effect | explicit no-effect conflict |

`canEmitCand1Pass` was not changed.

## Permanent fixture matrix

The interprocedural suite now covers:

- exact compatible return contract/body;
- owned return with an unrelated borrowed parameter omitted;
- destroy of one parameter with another parameter borrowed;
- explicit no-effect versus body borrow;
- owned versus borrowed return;
- destroy versus borrow;
- unknown body effect;
- conditional body effect;
- compatible annotation/body/contract;
- annotation/contract disagreement;
- return-borrow-origin disagreement;
- external declaration annotation without a trusted contract.

The compatible cases remain PASS. Every contradiction, unknown/conditional
case, and candidate-only external annotation remains INCOMPLETE. Malformed
contracts remain tool errors.

Conflict JSON now includes a deterministic reason, symbol, contract fact, body
fact, and parameter index where applicable. Reasons include return ownership
mismatch, return borrow-origin mismatch, parameter effect mismatch, explicit
no-effect mismatch, annotation/body mismatch, unknown body effect, and
conditional/unrepresentable body behavior.

## Hiredis measurements

The same seven translation units and source/profile were used for every mode.
The line-weighted verdict totals remain the H0 control totals:

| Mode | Analyzed LOC | PASS | FAIL | INCOMPLETE | Obligations | Unique locations | Runtime | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| H0 | 5,509 | 370 | 0 | 5,139 | 1,217 | 1,161 | 0.739s | 84,176 KB |
| H1 contracts only | 5,509 | 370 | 0 | 5,139 | 1,260 | 1,193 | 0.755s | 84,192 KB |
| H3 contracts + annotations | 5,509 | 370 | 0 | 5,139 | 1,262 | 1,195 | 0.743s | 84,200 KB |

Exact obligation deltas:

- H0 -> H1: 39 removed, 82 added, 1,178 unchanged;
- H0 -> H3: 39 removed, 84 added, 1,178 unchanged.

The 35 H1 conflicts classify as 28 unknown body effects, 6 parameter-effect
mismatches, and 1 return-ownership mismatch. H3 has 29 conflicts; all 29 are
annotation/body mismatch observations in the annotation-patched source. The
conflict count did not fall, and no function became observationally CLEAR.

Function-level observability remains non-authoritative:

| Mode | CLEAR | BLOCKED | VIOLATION | TOOL-UNKNOWN |
|---|---:|---:|---:|---:|
| H0 | 40 | 141 | 0 | 0 |
| H1 | 37 | 144 | 0 | 0 |
| H3 | 38 | 143 | 0 | 0 |

Observed `BLOCKED -> CLEAR` transitions: **0**.

## Soundness controls

- `scripts/check.sh`: PASS;
- complete CTest: 19/19 PASS;
- extended C&1 fuzz: 10,000 cases, 3,334 correct PASS, 3,333 correct FAIL,
  3,333 correct INCOMPLETE, 0 false PASS, 0 coverage gaps, 0 harness errors;
- pinned unmodified Hiredis ASan/UBSan CTest: 1/1 PASS, 15.93s, clean;
- nine Hiredis mutations: 9/9 independently ASan-confirmed temporal defects,
  9/9 C&1 INCOMPLETE, 0 PASS;
- ordinary Hiredis ABI/layout and tests remain unchanged from the pilot
  control;
- policy/evidence replay and contract trust-boundary tests pass.

The candidate-only external declaration annotation attack remains INCOMPLETE
(now reported as `unreviewed-declaration-annotation`; see
`docs/pilots/DECL-ANNOTATION-PROPAGATION.md`). Changing contract digests,
substituting a candidate bundle, changing source after evidence, and changing
policy inputs remain fail-closed under the existing evidence/policy tests.

## Disposition

The compatibility representation is qualified as a fail-closed precision and
diagnostic improvement, but the Hiredis adoption result is insufficient for a
Redis GO decision: there is no real Hiredis function-level BLOCKED-to-CLEAR
improvement and no TU-level coverage gain. #43 remains open for the next
bounded improvement. #39 remains open with its trust boundary clarified. #25
remains separate conservative precision debt, and #36 remains open.

Redis is **NO-GO**. The next evidence-backed candidates are #40 diagnostic and
pointer-parameter/alias precision work first, followed by separately qualified
#41 pointer-output ownership or #42 same-project cross-TU summaries only if a
new semantic profile is justified.
