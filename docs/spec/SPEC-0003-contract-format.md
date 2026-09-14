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
