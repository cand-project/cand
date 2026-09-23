# SPEC-0003: C& API Contract Format

- **Status:** Draft
- **Version:** 0.1.0
- **Depends on:** SPEC-0001, SPEC-0002

## 1. Purpose

C& cannot infer safe ownership effects for every external API from declarations alone. This specification defines the machine-readable contract format used to model those effects without changing the external library or compiler.

## 2. Design goals

The format must be human reviewable, machine validated, stable across analyzer frontends, expressive enough for common C ownership patterns, explicit about unsupported/conditional behavior, separate from code generation, and versioned/digestible for reproducible proof reports.

## 3. Contract bundle

A bundle is a YAML/JSON document conforming to `contracts/schema/cand-api-contract.schema.json`.

```yaml
schema: cand.api-contract/v1
name: libc-posix
version: "0.1.0"
platform:
  os: [linux]
  libc: [glibc, musl]
symbols: []
```

## 4. Symbol matching

A symbol entry identifies a function by C linkage name and optional platform/version constraints. Ambiguous matches fail contract resolution under strict mode.

## 5. Return ownership

Core return values are `none`, `owned`, `borrowed`, and `unknown`. Borrowed returns identify a lifetime origin; owned returns may identify an allocation family and nullability.

```yaml
returns:
  ownership: borrowed
  lifetime:
    from_param: 0
```

```yaml
returns:
  ownership: owned
  allocation_family: c-heap
  nullable: true
```

## 6. Parameter effects

Core effects are:

- `borrow_shared`
- `borrow_mut`
- `consumes`
- `destroys`
- `retains_borrow`
- `retains_ownership`
- `produces_out_owner`
- `no_ownership_effect`
- `unknown`

An omitted parameter effect is not the same as `no_ownership_effect`: omission
means that the contract supplies no fact for that parameter. An explicit
`no_ownership_effect` is a known fact and must be checked against any visible
same-translation-unit body. A trusted contract must not use an omitted field to
override or contradict a body-derived fact.

## 7. Conditional effects

Contracts may define bounded path-sensitive behavior based on return/parameter conditions. The condition language must remain schema-defined; arbitrary executable scripts are forbidden.

## 8. Allocation families

An allocation family pairs compatible allocators/destructors, for example `c-heap: malloc/calloc/realloc <-> free`. Destroying an owner through an incompatible family is a diagnostic.

## 9. Lifetime expressions

C&1 supports at least call-scoped lifetimes, `from_param: N`, static lifetime, retained-until-event where explicitly supported, and unknown. `unknown` cannot satisfy strict safe borrowing.

## 10. Varargs

Variadic functions require an explicit model when ownership-relevant pointers may appear in varargs. Otherwise strict safe code may use them only where ownership effects are proven irrelevant.

## 11. Function pointers/callbacks

Contracts may model callback/context retention and the release event. This capability may be staged after basic C&1 but must never be approximated unsafely.

## 12. Unknown fields/versioning

Under strict validation, unsupported schema versions, unknown fields, invalid enums, contradictory effects and conflicting equal-precedence contracts fail closed.

## 13. Trusted vs suggested contracts

Tools may infer or suggest contracts, but suggested contracts are not proof authority until explicitly reviewed and accepted into a trusted contract source. LLM-generated contracts are never trusted solely on confidence.

## 14. Project overrides

Projects may override a standard/vendor contract when wrappers/interposition change semantics. Overrides must identify the target and SHOULD record the reason/version.

## 15. Contract testing

Trusted bundles SHOULD include declaration checks, small runtime fixtures under sanitizers where appropriate, positive/negative static ownership cases and explicit platform/version constraints.

## 16. Release binding

A C& proof report MUST include cryptographic digests of every trusted contract bundle used. Contract changes invalidate dependent cached summaries/results.

## 17. Declaration-annotation review manifest

A declaration-annotation review manifest (ADR-0029) records that the C&
ownership annotations on a set of external (body-less) declarations have
been separately reviewed. A manifest is a YAML document with schema
`cand.annotation-review/v1` whose `symbols` entries use exactly the symbol
fact vocabulary of this specification:

```yaml
schema: cand.annotation-review/v1
name: hiredis-cand1-reviewed-h2
version: "1"
symbols:
  - symbol: reviewed_create
    kind: function
    returns:
      ownership: owned
      nullable: true
  - symbol: reviewed_destroy
    kind: function
    params:
      - index: 0
        effect: destroys
```

Semantics:

- A body-less declaration carrying ownership annotations seeds an external
  summary only when the manifest lists the symbol and the declaration's
  gathered facts equal the manifest facts exactly. Otherwise the calls fail
  closed with an `unreviewed-declaration-annotation` (or
  `conflicting-declaration-annotation`) obligation.
- Un-annotated declaration parameters mean `no_ownership_effect` (the
  annotation-language semantics); unlisted contract parameters mean
  `unknown`. Silence is compatible when merging with a contract; explicit
  facts must agree exactly, and disagreement fails closed.
- Visible bodies always take precedence; annotations never override them.
- In the agent path the manifest is a trusted input pinned by the policy's
  trusted pin list (path + sha256 + trust class), appears in evidence as
  `annotation_reviews`, and participates in the evidence freshness check.
- Suggested or LLM-generated manifests are not proof authority until
  reviewed and pinned, exactly as with contracts.

