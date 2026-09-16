# SPEC-0004: Machine-Agent Verification Protocol

- **Status:** Draft
- **Version:** 0.1.0
- **Depends on:** ADR-0003, ADR-0005, ADR-0006, ADR-0008, ADR-0009, SPEC-0001, SPEC-0002

## 1. Purpose

This specification defines the machine-facing contract that allows an LLM/coding agent to generate C, invoke C&, consume deterministic proof obligations, repair the program and obtain a scoped evidence result without parsing human-oriented diagnostics.

The protocol is model- and vendor-neutral.

The intended architecture is:

```text
requirements / architecture
          |
          v
   coding agent / LLM
          |
          v
 C source + C& metadata
          |
          v
   cand machine protocol
          |
     +----+----+
     |         |
  findings   evidence
     |         |
     v         v
 agent repair  build/test/review
```

The agent is a synthesis client. C& is the deterministic verifier.

P0.5 implements a narrow protocol slice: generated profile
`p0-temporal-lifecycle`, `cand.policy/v1`, `cand.agent-check/v1`,
`cand.evidence/v1`, policy diff, and evidence replay. It does not implement a
`cand1` soundness claim or every aspirational command below.

## 2. Normative principles

1. Machine output MUST be deterministic for identical relevant inputs.
2. Machine output MUST NOT require natural-language parsing to determine pass/fail or repair class.
3. Human-readable diagnostics MAY evolve in wording without changing stable machine semantics.
4. Diagnostic IDs, rule IDs and schema versions MUST be explicit.
5. Proof-policy changes MUST be represented separately from implementation findings.
6. An agent-generated assertion of safety has no proof status without C& evidence.

## 3. Required commands

The initial machine-capable CLI SHOULD expose equivalent functionality to:

```text
cand check --format json
cand check --agent --base origin/main --policy cand-policy.json --format json
cand explain <diagnostic-id> --format json
cand contract validate --format json
cand contract explain <symbol> --format json
cand policy diff --base <ref> --format json
cand report --format json
cand check --agent --emit-evidence evidence.json ...
cand evidence verify evidence.json
```

The listed non-P0.5 commands remain aspirational; implemented spellings are
normative for the current version.

## 4. Agent generation profile

C& SHOULD provide a strict generation profile for new machine-authored code.

Conceptually:

```bash
cand check --profile generated --level p0-temporal-lifecycle --format json
```

The profile SHOULD default to:

- strict checked scope for configured generated files/functions;
- unknown ownership-affecting calls as errors;
- unsupported constructs as errors for claimed scope;
- no implicit suppression creation;
- no automatic unsafe insertion;
- proof-policy regression checks when a base is supplied;
- deterministic evidence output.

P0.5 supports only `p0-temporal-lifecycle`, C11, an explicit non-empty source
scope, zero weakening budgets, and exact policy-pinned additional compiler
arguments. Unsupported levels/arguments are rejected. `--agent` requires
`--base origin/main` and a runner-provided `CAND_TRUSTED_BASE_SHA` matching the
resolved base commit. The environment value must come from protected CI
context; agent-controlled local output is not a trusted CI result.

The effective policy is strict `cand.policy/v1` JSON. Contract trust is
separate from contract contents and binds path, digest, and trust class.
Candidate contracts cannot support PASS. Semantic PASS and policy PASS are
distinct; a policy failure or `REVIEW_REQUIRED` cannot be reported as verified
agent success.

`cand.evidence/v1` binds checked source and project-local include contents,
verifier binary digest, frontend identity/arguments, base commit, effective
policy digest, scope, and used contract digests. `cand evidence verify`
validates bound inputs and reruns the exact analysis, comparing canonical
evidence. Its unkeyed digest is not a signature; trusted CI must select the
verifier binary and supply the trusted base. Evidence does not imply general C
memory safety.

A project may override these defaults explicitly, but the evidence record must capture the effective policy.

## 5. Result envelope

Machine output MUST use a versioned top-level envelope.

Illustrative shape:

```json
{
  "schema": "cand.check/v1",
  "cand_version": "0.x.y",
  "result": "pass",
  "safety_level": "cand1",
  "profile": "generated",
  "source_identity": "...",
  "findings": [],
  "policy_delta": {},
  "coverage": {},
  "unsupported": [],
  "evidence": {}
}
```

The exact JSON Schema will be versioned separately once implementation begins.

## 6. Finding object

Every enforcement finding MUST contain at least:

```text
id
rule_id
severity
safety_level
primary_location
message_key or machine-stable summary class
repair_class
```

Where applicable it SHOULD additionally contain:

```text
object_identity
allocation/origin location
owner origin
borrow origin
state transition
call trace
control-flow path summary
contract symbol/source
unsupported reason
related locations
candidate edits
policy escape information
```

### 6.1 Example use-after-destroy finding

```json
{
  "id": "CAND-T002",
  "rule_id": "cand1.no-use-after-death",
  "severity": "error",
  "safety_level": "cand1",
  "primary_location": {
    "file": "src/foo.c",
    "line": 42,
    "column": 9
  },
  "object": {
    "abstract_id": "obj:17",
    "origin": {"file": "src/foo.c", "line": 35},
    "state": "Dead"
  },
  "state_trace": [
    {"state": "Owned", "line": 35},
    {"state": "Dead", "line": 40},
    {"operation": "dereference", "line": 42}
  ],
  "repair_class": "SEMANTIC_REPAIR",
  "automatic_edit_available": false
}
```

The agent should be able to infer the necessary repair direction from structured state, not from prose alone.

## 7. Repair classes

Each finding MUST classify remediation using a stable category.

Minimum classes:

### `ANNOTATION_ONLY`

The implementation semantics are already consistent and the repair adds/corrects C& metadata only.

Potentially eligible for automatic fix.

### `SEMANTICS_PRESERVING_REFACTOR`

The change is proven not to alter relevant program behavior under the supported transformation model.

May be eligible for automatic fix only when C& has a sound transformation rule.

### `SEMANTIC_REPAIR`

Program ordering, lifetime, ownership transfer or behavior must change.

C& may suggest a patch but SHALL NOT silently apply it as a safe automatic fix.

### `CONTRACT_REQUIRED`

The verifier lacks trusted ownership semantics for an API boundary.

The agent may propose a candidate contract, but cannot self-promote it to trusted proof authority.

### `UNSAFE_OR_UNSUPPORTED`

The operation cannot currently be proven at the requested level.

An unsafe boundary may be possible but is a policy action, not an implementation repair.

### `POLICY_CHANGE_REQUIRED`

Passing requires a change to safety level, checked scope, suppression, unsafe budget, trusted contract policy or another proof-policy surface.

Agent mode MUST surface this separately.

## 8. Candidate edits

For safe mechanical fixes, C& MAY return machine-applicable edits.

Illustrative representation:

```json
{
  "candidate_edits": [
    {
      "kind": "insert_annotation",
      "confidence_class": "proved_safe",
      "file": "src/foo.c",
      "range": {...},
      "replacement": "CAND_OWN"
    }
  ]
}
```

C& SHOULD avoid probabilistic confidence scores for proof decisions. A transformation is either within a proved-safe edit class or it is not.

LLMs may generate broader candidate repairs themselves; those repairs become new source and must be rechecked normally.

## 9. Iteration semantics

The expected agent loop is:

```text
generate
  -> check
  -> findings
  -> edit
  -> check
  -> ...
  -> pass
  -> build/tests/sanitizers
  -> evidence
```

C& SHOULD optimize for repeated invocation by caching translation-unit and summary state where correctness permits.

Caching MUST NOT change pass/fail semantics.

## 10. Agent convergence

C& SHOULD make repeated diagnostics stable enough that an agent can tell whether it is converging.

Finding identity SHOULD be based on semantic rule/object/location information rather than random run-local IDs alone.

Reports MAY include:

- findings added;
- findings resolved;
- findings unchanged;
- checked coverage delta;
- unsupported delta;
- unsafe boundary delta;
- contract/policy delta.

This lets an agent distinguish real repair from merely moving a defect.

## 11. Proof-policy delta

When `--base` or equivalent is supplied, machine output MUST separately report policy-affecting changes defined by ADR-0009.

Illustrative shape:

```json
{
  "policy_delta": {
    "new_unsafe_boundaries": 0,
    "new_suppressions": 0,
    "safety_level_reduced": false,
    "checked_scope_delta": "+3 functions",
    "trusted_contract_changes": [],
    "mandatory_proof_fixture_changes": []
  }
}
```

A result may be semantically clean but policy-blocked.

Therefore top-level status SHOULD distinguish at least:

```text
pass
fail-findings
fail-policy
fail-unsupported
internal-error
```

## 12. Contract proposal workflow

An agent-facing contract workflow SHOULD support candidate generation without conflating it with trust.

Conceptually:

```bash
cand contract propose <header-or-symbol> --format json
cand contract validate candidate.yaml
cand contract diff --base trusted-bundle.yaml candidate.yaml
```

A candidate contract SHOULD record provenance such as:

- generated_by: model/agent identifier if known;
- evidence sources: header/body/docs/call-sites;
- created_at;
- trust_class: candidate;
- review status.

C& strict proof MUST ignore candidate-only facts when the active policy requires trusted contracts.

## 13. Evidence artifact

A successful verification SHOULD be exportable as a versioned immutable evidence object.

Minimum fields:

```text
schema version
C& version
source revision/content digest
analysis frontend/version
final compiler profile if known
safety level
profile/policy digest
checked scope
unsafe boundaries
unsupported scope
suppression/baseline state
contract digests + trust classes
mandatory proof-suite version
finding counts
policy delta
result
```

Evidence SHOULD be suitable for CI artifact retention and later signing/attestation.

## 14. Human review projection

Although this SPEC is machine-oriented, C& SHOULD provide a review projection optimized for humans supervising agents.

A reviewer should be able to see a concise summary such as:

```text
Implementation changes: 184 lines
C&1 findings: 0
Checked functions: +12
New unsafe boundaries: 0
New suppressions: 0
Unsupported functions: -2
Trusted contract changes: 1 (review required)
Semantic C& fix suggestions applied automatically: 0
Annotation-only safe fixes: 7
```

This lets humans review trust changes rather than every generated line equally.

## 15. Security considerations

### 15.1 Untrusted generator

The coding agent is untrusted. Malicious, mistaken or reward-hacking output must be treated exactly like arbitrary source changes.

### 15.2 Diagnostic injection

Source comments, string literals, identifiers or build output MUST NOT be able to forge C& machine results. Structured verifier output comes from the C& process, not source-controlled text.

### 15.3 Policy manipulation

Agent-mode verification must detect proof-policy weakening as defined by ADR-0009.

### 15.4 Contract poisoning

Candidate contracts cannot become trusted merely by being placed in the repository. Trust status is policy-controlled and evidence-bound.

### 15.5 Tool invocation

C& should avoid requiring an agent to execute arbitrary code merely to understand diagnostics. Build/test execution is a separate project-level concern.

## 16. Performance

Agent loops may invoke C& much more frequently than traditional human CI workflows.

The implementation SHOULD therefore support:

- changed-file/translation-unit analysis;
- cached semantic summaries;
- dependency-aware invalidation;
- parallel checking;
- fast local machine output;
- full clean release verification as a separate stronger gate.

Fast incremental analysis may reuse cached proof components only when their dependencies are unchanged and validated.

## 17. Model neutrality

C& integrations MUST remain usable from shell scripts and ordinary process APIs.

No safety capability may require a specific proprietary model or agent.

Reference integrations MAY be published for popular agent environments, but the normative protocol remains open and implementation-independent.

## 18. Acceptance suite

SPEC-0004 is implemented when automated tests demonstrate:

1. deterministic JSON finding output;
2. stable diagnostic/rule identity across repeated identical runs;
3. a coding agent can repair mandatory temporal fixtures using only machine output plus source access;
4. passing output produces an evidence artifact;
5. adding an unapproved unsafe boundary causes `fail-policy` rather than false success in agent mode;
6. AI-generated candidate contract facts remain excluded from trusted proof until promoted;
7. incremental analysis produces the same semantic result as a clean full analysis for the tested dependency graph;
8. human and machine output agree on pass/fail while permitting different presentation wording.
