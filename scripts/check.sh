#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "==> JSON schema syntax"
python3 -m json.tool contracts/schema/cand-api-contract.schema.json >/dev/null

echo "==> YAML syntax"
ruby -e 'require "yaml"; ARGV.each { |f| YAML.safe_load(File.read(f), permitted_classes: [], permitted_symbols: [], aliases: false) }' \
  contracts/safety-levels.yaml contracts/diagnostics.yaml contracts/libc.yaml

echo "==> Required architecture invariants"
grep -q "no new compiler" README.md
grep -q "MUST NOT become a compiler fork" README.md
grep -q "Not a C Compiler" docs/adr/ADR-0001-pipeline-safety-layer.md

echo "==> GCC ordinary-C compatibility"
gcc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only examples/ownership.c

echo "==> Clang ordinary-C compatibility"
clang -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only examples/ownership.c

echo "==> Clang analysis-annotation compatibility"
clang -std=c11 -Wall -Wextra -Werror -DCAND_ANALYSIS=1 -Iinclude -fsyntax-only examples/ownership.c

echo "All repository checks passed."
