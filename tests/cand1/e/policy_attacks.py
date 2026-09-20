#!/usr/bin/env python3
"""Independent generated-policy and evidence-authority attack campaign."""

from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from independent import PREFIX


SAFE = PREFIX + r'''
CAND_RETURNS_OWN int *make(void) { return malloc(sizeof(int)); }
int main(void) {
    int *owner CAND_OWN = make();
    int value = *owner;
    free(owner);
    return value;
}
'''


def base_policy() -> dict:
    return {
        "schema": "cand.policy/v1", "profile": "generated", "safety_level": "cand1",
        "base_ref": "origin/main",
        "budgets": {"new_unsafe_boundaries": 0, "new_suppressions": 0,
                     "safety_level_reductions": 0, "checked_scope_decrease": 0,
                     "unsupported_scope_increase": 0},
        "unsupported": {"allow_in_verified_success": False},
        "contracts": {"trusted_changes_require_review": True, "trusted": []},
        "scope": {"files": ["case.c"]}, "frontend": {"standard": "c11", "arguments": []},
    }


def call(cand: Path, work: Path, env: dict[str, str], *extra: str) -> tuple[int, dict]:
    proc = subprocess.run(
        [str(cand.resolve()), *extra], cwd=work, env=env,
        capture_output=True, text=True, timeout=30, check=False,
    )
    try:
        return proc.returncode, json.loads(proc.stdout)
    except json.JSONDecodeError:
        return proc.returncode, {"result": "tool-error", "stderr": proc.stderr[-500:]}


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("cand", type=Path)
    args = parser.parse_args()
    outcomes: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="cand1-e-policy-") as name:
        work = Path(name)
        (work / "case.c").write_text(SAFE)
        policy = base_policy()
        (work / "cand-policy.json").write_text(json.dumps(policy, indent=2) + "\n")
        for command in (
            ["git", "init", "-q", "-b", "main"],
            ["git", "config", "user.name", "CAND-E"],
            ["git", "config", "user.email", "cand-e@example.invalid"],
        ):
            subprocess.run(command, cwd=work, check=True)
        subprocess.run(["git", "add", "."], cwd=work, check=True)
        subprocess.run(["git", "commit", "-qm", "policy baseline"], cwd=work, check=True)
        base = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=work, text=True).strip()
        subprocess.run(["git", "update-ref", "refs/remotes/origin/main", base], cwd=work, check=True)
        env = os.environ.copy()
        env["CAND_TRUSTED_BASE_SHA"] = base

        rc, report = call(args.cand, work, env, "check", "--agent", "--level=cand1", "--format=json",
                          "--base", "origin/main", "--policy", "cand-policy.json", "case.c", "--", "-std=c11")
        outcomes.append({"id": "baseline-pass", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})
        rc, report = call(args.cand, work, env, "check", "--level=cand1", "--format=json", "case.c", "--", "-std=c11")
        outcomes.append({"id": "non-agent-cand1", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})

        mutations = {
            "profile": lambda p: p.update(profile="semantic"),
            "safety-level": lambda p: p.update(safety_level="p0-temporal-lifecycle"),
            "allow-unsupported": lambda p: p["unsupported"].update(allow_in_verified_success=True),
            "suppression-budget": lambda p: p["budgets"].update(new_suppressions=1),
            "scope-decrease": lambda p: p["scope"].update(files=[]),
            "frontend-path": lambda p: p["frontend"].update(arguments=["-I/tmp"]),
        }
        for ident, mutate in mutations.items():
            candidate = copy.deepcopy(policy)
            mutate(candidate)
            (work / "cand-policy.json").write_text(json.dumps(candidate, indent=2) + "\n")
            rc, report = call(args.cand, work, env, "check", "--agent", "--level=cand1", "--format=json",
                              "--base", "origin/main", "--policy", "cand-policy.json", "case.c", "--", "-std=c11")
            outcomes.append({"id": "policy-" + ident, "result": report.get("result"), "detail": report.get("detail"), "exit": rc})
        (work / "cand-policy.json").write_text(json.dumps(policy, indent=2) + "\n")
        env_bad = dict(env, CAND_TRUSTED_BASE_SHA="0" * 40, CPATH="/tmp")
        rc, report = call(args.cand, work, env_bad, "check", "--agent", "--level=cand1", "--format=json",
                          "--base", "origin/main", "--policy", "cand-policy.json", "case.c", "--", "-std=c11")
        outcomes.append({"id": "wrong-base-and-environment", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})

        evidence = work / "evidence.json"
        rc, report = call(args.cand, work, env, "check", "--agent", "--level=cand1", "--format=json",
                          "--base", "origin/main", "--policy", "cand-policy.json", "--emit-evidence", "evidence.json",
                          "case.c", "--", "-std=c11")
        outcomes.append({"id": "evidence-emit", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})
        rc, report = call(args.cand, work, env, "evidence", "verify", "evidence.json")
        outcomes.append({"id": "evidence-valid-replay", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})

        (work / "case.c").write_text(SAFE + "\nint changed_after_evidence = 1;\n")
        rc, report = call(args.cand, work, env, "evidence", "verify", "evidence.json")
        outcomes.append({"id": "source-after-evidence", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})
        (work / "case.c").write_text(SAFE)
        changed = copy.deepcopy(policy)
        changed["frontend"]["arguments"] = ["-fno-builtin"]
        (work / "cand-policy.json").write_text(json.dumps(changed, indent=2) + "\n")
        rc, report = call(args.cand, work, env, "evidence", "verify", "evidence.json")
        outcomes.append({"id": "policy-after-evidence", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})
        (work / "cand-policy.json").write_text(json.dumps(policy, indent=2) + "\n")

        mutated = work / "cand-mutated"
        shutil.copy2(args.cand, mutated)
        data = bytearray(mutated.read_bytes())
        data[-1] ^= 1
        mutated.write_bytes(data)
        rc, report = call(mutated, work, env, "evidence", "verify", "evidence.json")
        outcomes.append({"id": "verifier-substitution", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})

        tampered = json.loads(evidence.read_text())
        tampered["frontend"]["arguments"] = ["-std=c11", "-fno-builtin"]
        tampered.pop("integrity_sha256")
        payload = json.dumps(tampered, sort_keys=True, indent=2, ensure_ascii=False) + "\n"
        tampered["integrity_sha256"] = hashlib.sha256(payload.encode()).hexdigest()
        (work / "tampered.json").write_text(json.dumps(tampered, indent=2) + "\n")
        rc, report = call(args.cand, work, env, "evidence", "verify", "tampered.json")
        outcomes.append({"id": "recomputed-tampered-evidence", "result": report.get("result"), "detail": report.get("detail"), "exit": rc})
    print(json.dumps({"schema": "cand1-e.policy-attacks/v1", "outcomes": outcomes}, sort_keys=True))
    bad = [x for x in outcomes if x["id"] == "baseline-pass" and x["result"] != "pass"]
    bad += [x for x in outcomes if x["id"] != "baseline-pass" and x["result"] == "pass"]
    return int(bool(bad))


if __name__ == "__main__":
    raise SystemExit(main())
