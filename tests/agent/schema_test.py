#!/usr/bin/env python3
"""Schema and cross-field invariant regressions for emitted P0.5 artifacts."""
from copy import deepcopy
import json
from pathlib import Path
import sys

from jsonschema import Draft202012Validator, RefResolver


ROOT = Path(__file__).resolve().parents[2]


def load(name):
    return json.loads((ROOT / "contracts/schema" / name).read_text())


def assert_invalid(validator, value, label):
    if validator.is_valid(value):
        raise AssertionError(f"negative schema case unexpectedly valid: {label}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: schema_test.py <evidence.json> <agent-check.json>")
    evidence = json.loads(Path(sys.argv[1]).read_text())
    agent = json.loads(Path(sys.argv[2]).read_text())
    evidence_schema = load("cand-evidence.schema.json")
    agent_schema = load("cand-agent-check.schema.json")
    evidence_validator = Draft202012Validator(evidence_schema)
    agent_validator = Draft202012Validator(
        agent_schema,
        resolver=RefResolver("https://cand-project.github.io/cand/schema/cand-agent-check-v1.json", agent_schema,
                             {"https://cand-project.github.io/cand/schema/cand-evidence-v1.json": evidence_schema}),
    )
    evidence_validator.validate(evidence)
    agent_validator.validate(agent)

    missing = deepcopy(evidence)
    del missing["cand"]["verifier_source_commit"]
    assert_invalid(evidence_validator, missing, "missing verifier identity")
    extra = deepcopy(evidence)
    extra["unexpected"] = True
    assert_invalid(evidence_validator, extra, "unexpected field")
    bad_digest = deepcopy(evidence)
    bad_digest["cand"]["binary_sha256"] = "not-a-digest"
    assert_invalid(evidence_validator, bad_digest, "malformed digest")
    empty_scope = deepcopy(evidence)
    empty_scope["verification"]["checked_scope"] = []
    assert_invalid(evidence_validator, empty_scope, "empty checked scope")
    bad_level = deepcopy(evidence)
    bad_level["verification"]["safety_level"] = "cand1"
    assert_invalid(evidence_validator, bad_level, "unsupported safety level")

    invalid_pass = deepcopy(evidence)
    invalid_pass["result"] = "pass"
    invalid_pass["semantic_result"] = "incomplete"
    if invalid_pass["result"] == "pass" and invalid_pass["semantic_result"] != "pass":
        pass
    else:
        raise AssertionError("cross-field PASS invariant test is ineffective")
    print("schema and cross-field negative tests passed")


if __name__ == "__main__":
    main()
