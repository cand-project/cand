#!/usr/bin/env python3
"""Ephemeral generated-profile repository for the real cand1 PASS predicate."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

# Fixed reviewed-annotation manifest for the fuzz workspace (ADR-0029).
# The symbol facts never change per case, so no generated case can promote
# an unreviewed annotation into the trusted base: a declaration whose facts
# differ from these entries (or is absent) stays fail-closed INCOMPLETE.
EXTERN_REVIEW_MANIFEST = """\
schema: cand.annotation-review/v1
name: cand1-fuzz-extern-review
version: "1"
symbols:
  - symbol: cand1_extern_create
    kind: function
    returns:
      ownership: owned
  - symbol: cand1_extern_destroy
    kind: function
    params:
      - index: 0
        effect: destroys
  - symbol: cand1_extern_view
    kind: function
    returns:
      ownership: borrowed
      lifetime:
        from_param: 0
    params:
      - index: 0
        effect: borrow_shared
"""

# Definitions for the reviewed external symbols. Compiled into the ASan
# binary only: the verifier analyzes case.c alone, where these symbols are
# body-less annotated declarations.
EXTERN_HELPER_C = """\
#include <stdlib.h>

void *cand1_extern_create(void) { return malloc(sizeof(int)); }
void cand1_extern_destroy(void *p) { free(p); }
void *cand1_extern_view(void *p) { return p; }
"""


class StrictWorkspace:
    def __init__(self, cand: Path):
        self.cand = cand.resolve()
        self._temp = tempfile.TemporaryDirectory(prefix="cand1-strict-")
        self.path = Path(self._temp.name)
        self._init_repo()

    def _init_repo(self) -> None:
        review_digest = hashlib.sha256(EXTERN_REVIEW_MANIFEST.encode("utf-8")).hexdigest()
        policy = {
            "schema": "cand.policy/v1",
            "profile": "generated",
            "safety_level": "cand1",
            "base_ref": "origin/main",
            "budgets": {
                "new_unsafe_boundaries": 0,
                "new_suppressions": 0,
                "safety_level_reductions": 0,
                "checked_scope_decrease": 0,
                "unsupported_scope_increase": 0,
            },
            "unsupported": {"allow_in_verified_success": False},
            "contracts": {
                "trusted_changes_require_review": True,
                "trusted": [
                    {
                        "path": "extern-review.yaml",
                        "sha256": review_digest,
                        "trust_class": "reviewed",
                    }
                ],
            },
            "scope": {"files": ["case.c"]},
            "frontend": {"standard": "c11", "arguments": []},
        }
        (self.path / "case.c").write_text("int cand1_placeholder(void) { return 0; }\n", encoding="utf-8")
        (self.path / "extern-review.yaml").write_text(EXTERN_REVIEW_MANIFEST, encoding="utf-8")
        (self.path / "cand-policy.json").write_text(
            json.dumps(policy, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        for command in (
            ["git", "init", "-q", "-b", "main"],
            ["git", "config", "user.name", "CAND-fuzz"],
            ["git", "config", "user.email", "cand-fuzz@example.invalid"],
        ):
            subprocess.run(command, cwd=self.path, check=True, capture_output=True)
        subprocess.run(["git", "add", "."], cwd=self.path, check=True, capture_output=True)
        subprocess.run(["git", "commit", "-qm", "qualification baseline"], cwd=self.path,
                       check=True, capture_output=True)
        subprocess.run(["git", "update-ref", "refs/remotes/origin/main", "HEAD"],
                       cwd=self.path, check=True, capture_output=True)
        self.base_sha = subprocess.check_output(
            ["git", "rev-parse", "origin/main"], cwd=self.path, text=True
        ).strip()

    def run(self, source: str) -> tuple[dict, int, str, str]:
        (self.path / "case.c").write_text(source, encoding="utf-8")
        env = os.environ.copy()
        env["CAND_TRUSTED_BASE_SHA"] = self.base_sha
        proc = subprocess.run(
            [str(self.cand), "check", "--agent", "--level=cand1", "--format=json",
             "--base", "origin/main", "--policy", "cand-policy.json",
             "--annotation-review=extern-review.yaml", "case.c", "--", "-std=c11"],
            cwd=self.path, env=env, capture_output=True, text=True, timeout=30, check=False,
        )
        try:
            report = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"cand emitted invalid JSON: {proc.stdout!r} {proc.stderr!r}") from exc
        return report, proc.returncode, proc.stdout, proc.stderr

    def close(self) -> None:
        self._temp.cleanup()

    def __enter__(self) -> "StrictWorkspace":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()
