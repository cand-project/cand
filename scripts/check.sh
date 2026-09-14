#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "==> JSON schema syntax"
python3 -m json.tool contracts/schema/cand-api-contract.schema.json >/dev/null

echo "==> YAML syntax"
ruby -e 'require "yaml"; ARGV.each { |f| YAML.safe_load(File.read(f), permitted_classes: [], permitted_symbols: [], aliases: false) }' \
  contracts/safety-levels.yaml contracts/diagnostics.yaml contracts/libc.yaml contracts/agent-policy.yaml

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
test "$version" = "0.1.0"
grep -Fq 'Current version:** `0.1.0`' README.md
grep -Fq '## 0.1.0 — 2026-09-14' CHANGELOG.md

echo "==> Required architecture invariants"
grep -q "no new compiler" README.md
grep -q "MUST NOT become a compiler fork" README.md
grep -q "Not a C Compiler" docs/adr/ADR-0001-pipeline-safety-layer.md
grep -Fq 'does **not** claim that C&1' README.md
grep -Fq 'LLMs synthesize. C& verifies.' README.md
grep -Fq 'The LLM is **not** part of the trusted computing base.' README.md
grep -Fq 'proof-policy change' README.md
test -f docs/adr/ADR-0008-llm-first-synthesis-and-verification.md
test -f docs/adr/ADR-0009-agent-proof-policy.md
test -f docs/spec/SPEC-0004-machine-agent-protocol.md
test -f contracts/agent-policy.yaml

echo "==> Agent-policy baseline"
grep -Fq 'new_unsafe_boundaries: 0' contracts/agent-policy.yaml
grep -Fq 'new_suppressions: 0' contracts/agent-policy.yaml
grep -Fq 'trusted_contract_promotion: forbidden' contracts/agent-policy.yaml
grep -Fq 'llm_generated_safety_claims: no_proof_status' contracts/agent-policy.yaml

echo "==> GCC ordinary-C compatibility"
gcc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only examples/ownership.c

echo "==> Clang ordinary-C compatibility"
clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only examples/ownership.c

echo "==> Clang analysis-annotation compatibility"
clang -std=c11 -Wall -Wextra -Werror -DCAND_ANALYSIS=1 -Iinclude -fsyntax-only examples/ownership.c

echo "All repository checks passed for C& ${version}."
