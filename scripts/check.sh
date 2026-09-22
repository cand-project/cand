#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "==> JSON schema syntax"
python3 -m json.tool contracts/schema/cand-api-contract.schema.json >/dev/null
python3 -m json.tool contracts/schema/cand-check.schema.json >/dev/null
python3 -m json.tool contracts/schema/cand-policy.schema.json >/dev/null
python3 -m json.tool contracts/schema/cand-evidence.schema.json >/dev/null
python3 -m json.tool toolchains/cand1-v1-linux-x86_64.json >/dev/null
python3 -m json.tool contracts/schema/cand-agent-check.schema.json >/dev/null
python3 -m json.tool cand-policy.json >/dev/null
python3 -c 'import jsonschema' >/dev/null

echo "==> YAML syntax"
ruby -e 'require "yaml"; ARGV.each { |f| YAML.safe_load(File.read(f), permitted_classes: [], permitted_symbols: [], aliases: false) }' \
  contracts/safety-levels.yaml contracts/diagnostics.yaml contracts/libc.yaml contracts/libc-borrow.yaml contracts/agent-policy.yaml

echo "==> Agent evidence verifier syntax"
python3 -m py_compile .github/trusted/attest.py
python3 tests/agent/attestation_test.py
test -f .github/trusted/verifier-surface.json
python3 -m json.tool .github/trusted/verifier-surface.json >/dev/null

echo "==> SVG syntax and canonical assets"
python3 - <<'PY'
from pathlib import Path
import xml.etree.ElementTree as ET

assets = [
    Path("docs/assets/cand-logo.svg"),
    Path("docs/assets/cand-purpose.svg"),
    Path("docs/assets/cand-pipeline.svg"),
]
for asset in assets:
    if not asset.is_file():
        raise SystemExit(f"missing canonical visual asset: {asset}")
    ET.parse(asset)

readme = Path("README.md").read_text(encoding="utf-8")
for asset in assets:
    if str(asset) not in readme:
        raise SystemExit(f"README does not reference canonical visual asset: {asset}")
PY

echo "==> Version metadata"
version="$(tr -d '[:space:]' < VERSION)"
test "$version" = "0.2.0"
grep -Fq 'Current version:** `0.2.0`' README.md
grep -Fq '## 0.2.0 — 2026-09-21' CHANGELOG.md

echo "==> Required architecture invariants"
grep -q "no new compiler" README.md
grep -q "MUST NOT become a compiler fork" README.md
grep -q "Not a C Compiler" docs/adr/ADR-0001-pipeline-safety-layer.md
grep -Fq 'does **not** claim that C&1' README.md
grep -Fq 'LLMs synthesize. C& verifies.' README.md
grep -Fq 'The LLM is **not** part of the trusted computing base.' README.md
grep -Fq 'proof-policy change' README.md
! grep -Fq 'obj["safety_level"] = "cand1"' src/cand.cpp
grep -Fq 'stable `cand1.*` rule-ID namespace is retained' README.md
test -f docs/adr/ADR-0008-llm-first-synthesis-and-verification.md
test -f docs/adr/ADR-0009-agent-proof-policy.md
test -f docs/spec/SPEC-0004-machine-agent-protocol.md
test -f contracts/agent-policy.yaml

echo "==> Agent-policy baseline"
grep -Fq 'new_unsafe_boundaries: 0' contracts/agent-policy.yaml
grep -Fq 'new_suppressions: 0' contracts/agent-policy.yaml
grep -Fq 'trusted_contract_promotion: forbidden' contracts/agent-policy.yaml
grep -Fq 'llm_generated_safety_claims: no_proof_status' contracts/agent-policy.yaml

echo "==> PASS-path audit drift guard (docs/CAND1-PASS-PATH-AUDIT.md)"
bash scripts/pass-path-guard.sh

echo "==> CVE replay registry structure"
python3 - <<'PY'
import json, os, sys

registry = json.load(open("tests/cve-replay/registry.json"))
assert registry["schema"] == "cand.cve-replay/v2", "unknown registry schema"
assert isinstance(registry["entries"], list) and registry["entries"], "no entries"

allowed_status = {"validated", "candidate"}
allowed_expected = {"DETECTED", "BOUNDED-INCOMPLETE"}
for entry in registry["entries"]:
    for field in ("id", "cwe", "defect", "project", "repo",
                  "vulnerable_commit", "fix_commit", "driver", "prepare",
                  "asan_command", "cand_sources", "cand_flags",
                  "asan_site", "expected", "status", "analysis"):
        assert entry.get(field) is not None, f"{entry.get('id', '?')}: missing field {field}"
    assert entry["status"] in allowed_status, entry["status"]
    assert entry["expected"] in allowed_expected, entry["expected"]
    assert isinstance(entry["cand_sources"], list) and entry["cand_sources"], f"{entry['id']}: cand_sources must be a non-empty list"
    assert isinstance(entry["cand_flags"], list), f"{entry['id']}: cand_flags must be a list"
    for source in entry["cand_sources"]:
        assert not source.startswith("/"), f"{entry['id']}: cand_sources must be clone-relative: {source}"
    if entry["status"] == "validated":
        assert entry.get("validated_at"), f"{entry['id']}: validated entry needs validated_at"
    driver = os.path.join("tests/cve-replay", entry["driver"])
    assert os.path.isfile(driver), f"{entry['id']}: driver missing: {driver}"
    assert entry["vulnerable_commit"] != entry["fix_commit"], f"{entry['id']}: commits must differ"

print(f"CVE replay registry: {len(registry['entries'])} entr{'y' if len(registry['entries']) == 1 else 'ies'} structurally valid")
PY

echo "==> Cumulative campaign accounting smoke test"
python3 - <<'PY'
import json, subprocess, sys, tempfile

with tempfile.TemporaryDirectory(prefix="cand-accumulate.") as tmp:
    # A synthetic provenance-carrying report; passing the SAME report twice
    # must not inflate the unique-case total, and a gate-failing report must
    # be rejected outright.
    report = {
        "schema": "cand.fuzz-report/v2",
        "seed": 424242,
        "cases": 3,
        "deterministic_json": True,
        "results": {"correct_fail": 1, "correct_incomplete": 1, "correct_pass": 1,
                    "coverage_gap": 0, "false_pass": 0, "false_positive": 0,
                    "harness_error": 0, "wrong_failure_class": 0},
        "provenance": {"cand_sha256": "a" * 64, "generator_sha256": "b" * 64,
                       "source_commit": "c" * 40},
    }
    good = f"{tmp}/good"
    import os
    os.makedirs(good)
    json.dump(report, open(f"{good}/report.json", "w"))
    out = subprocess.run([sys.executable, "tests/fuzz/accumulate.py", good, good],
                         capture_output=True, text=True, check=True).stdout
    cumulative = json.loads(out)
    assert cumulative["unique_cases"] == 3, cumulative["unique_cases"]
    assert cumulative["unique_runs"] == 1
    assert len(cumulative["runs"]) == 2  # both runs recorded, cases counted once
    assert len(cumulative["duplicate_runs_excluded"]) == 1

    bad = dict(report)
    bad["results"] = dict(report["results"], false_pass=1)
    baddir = f"{tmp}/bad"
    os.makedirs(baddir)
    json.dump(bad, open(f"{baddir}/report.json", "w"))
    rc = subprocess.run([sys.executable, "tests/fuzz/accumulate.py", baddir],
                        capture_output=True, text=True)
    assert rc.returncode != 0, "a false-PASS report must be rejected"

print("cumulative accounting: dedup and gate rejection verified")
PY

echo "==> GCC ordinary-C compatibility"
gcc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only examples/ownership.c

echo "==> Clang ordinary-C compatibility"
clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only examples/ownership.c

echo "==> Clang analysis-annotation compatibility"
clang -std=c11 -Wall -Wextra -Werror -DCAND_ANALYSIS=1 -Iinclude -fsyntax-only examples/ownership.c

echo "All repository checks passed for C& ${version}."
