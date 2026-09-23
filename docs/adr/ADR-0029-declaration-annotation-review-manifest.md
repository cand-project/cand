# ADR-0029 — Reviewed Declaration-Annotation Review Manifest (Issue #39)

Status: accepted (2026-09-23, milestone #39 Gate B)

## Context

Issue #39: C& ownership annotations on external (body-less) declarations do
not seed interprocedural summaries, so a reviewed annotation such as

```c
extern CAND_RETURNS_OWN int *reviewed_create(void);
```

leaves every call site INCOMPLETE, while the identical reviewed fact in a
contract bundle (`--contracts`) resolves the same boundary. Two trusted-input
paths therefore disagree for the same human review effort, and the
declaration-site vocabulary is strictly less useful than the contract
vocabulary with no soundness benefit: the checker already refuses every
hard case.

The soundness constraint is recorded in
`docs/pilots/hiredis/HIREDIS-CONTRACT-RECONCILIATION.md` and
`docs/SAFETY_CLAIMS.md`: source annotations — including LLM-generated ones —
are not trusted merely because they exist. A candidate author must not be
able to make its own code pass by writing annotations. Any propagation of
declaration-site facts into the trusted base therefore needs a trust path
that a candidate cannot forge.

The milestone #39 Gate A census
(`docs/pilots/DECL-ANNOTATION-PROPAGATION.md`) recorded the evidence bar:
across the five pilots, 41,198 annotation-relevant rows contain 21,536
XTU-eligible rows and 2,045 distinct symbols (sqlite alone: 77% of
its annotation-relevant rows addressable), so the propagation is worth a
bounded rule.

## Decision

Introduce one bounded rule: **a body-less declaration carrying C& ownership
annotations seeds an external summary only when a separately reviewed
annotation-review manifest lists that symbol with exactly the same facts.**
The manifest is a trusted input pinned exactly like a contract bundle.

1. **Manifest.** New CLI `--annotation-review <file>`, schema
   `cand.annotation-review/v1`, fact structure identical to the SPEC-0003
   contract format (shared parser, `parseSymbolFactsFile`). The manifest is
   the review artifact: the reviewer asserts "these declaration annotations
   are the reviewed facts".
2. **Seeding.** A body-less annotated declaration whose gathered facts
   (return effect, borrow origin, per-parameter effects) match the manifest
   entry exactly seeds a summary with a new `SummaryOrigin::
   AnnotationTrusted`. Anything else — no manifest, symbol absent, any fact
   disagreement, contradictory facts across redeclarations, out-of-range
   borrow index — never seeds and reports a distinct fail-closed obligation
   kind (`unreviewed-declaration-annotation:<sym>` or
   `conflicting-declaration-annotation:<sym>`) so adopters know the
   annotation still needs review. Verdicts are unchanged from the
   no-annotation baseline; only the kind is refined.
3. **Merge with contracts.** Seeding runs before contract loading. When a
   symbol has both, agreement is exact fact equality with
   contract-silence-compatible semantics (`annotationSummaryMatchesContract`):
   on agreement the contract summary stands with its ExternalTrusted
   provenance; on disagreement the symbol fails closed with a
   `contract-body-conflict` obligation whose reason is
   `annotation/contract mismatch`.
4. **None/Unknown semantics.** Un-annotated declaration parameters mean
   "no ownership effect" (`None`, the annotation-language semantics shared
   with the same-TU path); unlisted contract parameters mean `Unknown`
   (contract-format semantics). Silence is compatible in merges; explicit
   facts must agree exactly. An annotation manifest whose reviewed facts are
   silent on a parameter can therefore legitimately resolve a call that the
   equivalent-incomplete contract leaves INCOMPLETE — the difference is the
   reviewed artifacts' completeness, not the mechanism.
5. **Body precedence.** Visible bodies always win: `hasBody()` declaration
   chains are never seeded and keep following the existing
   SummaryBuilder/contract reconciliation. Annotations never override a
   body.
6. **Scope guards.** Seeding is refused regardless of the manifest for:
   non-C translation units (`LangOpts.CPlusPlus`), K&R
   unspecified-parameter declarations, `realloc` (name-based, mirroring the
   contract loader), and any pointer-to-pointer parameter or return shape
   (including decayed `T *[]` and pointer-to-array-of-pointer) — issue #41
   scope. These keep today's unknown-call obligations unchanged.
7. **Policy path.** The agent path validates the manifest against the
   effective policy's trusted pin list (path + sha256 + trust class) exactly
   like a contract bundle (`validateAnnotationReview`); an unpinned,
   candidate, or substituted manifest fails the policy
   (`annotation-review-set-substitution`), forces review-required, and never
   reaches the analyzer. Validated manifests appear as `annotation_reviews`
   evidence entries, participate in the PASS trust-class gate, and are
   re-verified by the evidence freshness check (post-attestation mutation
   reports `stale`).

## Consequences

- The recorded attack control holds verbatim: a candidate-only external
  declaration annotation (no manifest) remains INCOMPLETE. The
  `unreviewed-declaration-annotation` kind makes the fail-closed state more
  legible, not less.
- Reviewed declaration annotations and reviewed contracts now deliver the
  same trusted facts. On the hiredis full-project replay the two modes
  differ by eight obligations out of ~1,050, every one attributable to the
  designed body-precedence and None/Unknown-silence rules (see the evidence
  document).
- The nine-mutation hiredis corpus moves from "9/9 INCOMPLETE" (historical
  gate) to "9/9 FAIL" under annotations + manifest: the same reviewed facts
  now detect all nine ASan-confirmed temporal defects. Confirmed defect +
  authoritative PASS remains 0.
- Zero-annotation corpora are byte-identical before/after (five-pilot
  invariant).
- Interpretation decisions flagged for reviewer attention:
  - **D1:** the manifest exists at all (versus refusing declaration-site
    propagation entirely);
  - **D2:** manifest facts must equal annotation facts exactly (versus
    subset/superset rules);
  - **D3:** un-annotated parameters mean `None`, not `Unknown`;
  - **D4:** annotation/contract disagreement fails closed rather than
    preferring either source;
  - **D5:** the manifest is pinned through the same `contracts.trusted`
    policy pin list rather than a new policy section (schema stability);
  - **D6:** the `realloc` name-based guard mirrors the contract loader
    rather than modeling conditional realloc semantics.
