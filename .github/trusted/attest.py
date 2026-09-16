#!/usr/bin/env python3
"""Trusted PR attestation driver for C&.

This script is executed from a pull_request_target workflow whose contents and
cand binary come from the protected PR base.  The candidate checkout is data,
not authority: no candidate build script or executable is invoked here.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.error
import urllib.request

HEX_SHA_RE = re.compile(r"^[0-9a-f]{40}$")

SENSITIVE_EXACT = {
    "cand-policy.json",
    "CMakeLists.txt",
    "VERSION",
}
SENSITIVE_PREFIXES = (
    ".github/",
    "cmake/",
    "contracts/",
    "include/cand/",
    "scripts/",
    "src/",
    "tests/",
)
PATH_FLAGS = {"-I", "-iquote", "-isystem", "-idirafter", "-include", "-imacros", "-isysroot"}


class AttestationError(RuntimeError):
    pass


def run(cmd: list[str], *, cwd: Path, env: dict[str, str] | None = None,
        check: bool = True) -> subprocess.CompletedProcess[str]:
    process = subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and process.returncode != 0:
        raise AttestationError(
            f"command failed ({process.returncode}): {' '.join(cmd)}\n{process.stderr}"
        )
    return process


def load_json(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as stream:
            value = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        raise AttestationError(f"cannot parse {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise AttestationError(f"{path} must contain a JSON object")
    return value


def require_repo_file(root: Path, relative: str, what: str) -> Path:
    if not relative or relative.startswith("/"):
        raise AttestationError(f"{what} must use a non-empty relative path: {relative!r}")
    lexical = Path(relative)
    if ".." in lexical.parts:
        raise AttestationError(f"{what} escapes the candidate workspace: {relative!r}")
    path = root / lexical
    if path.is_symlink():
        raise AttestationError(f"{what} must not be a symlink: {relative}")
    try:
        resolved = path.resolve(strict=True)
        repo = root.resolve(strict=True)
    except OSError as exc:
        raise AttestationError(f"cannot resolve {what} {relative!r}: {exc}") from exc
    if resolved != repo and repo not in resolved.parents:
        raise AttestationError(f"{what} resolves outside the candidate workspace: {relative}")
    if not resolved.is_file():
        raise AttestationError(f"{what} is not a regular file: {relative}")
    return resolved


def validate_frontend_paths(root: Path, arguments: list[str]) -> None:
    """Reject candidate-selected include/config files outside the checkout.

    Default toolchain headers are intentionally not enumerated here; they are
    part of the supported-toolchain/reproducible-build follow-up.  Explicit
    candidate-selected file-system inputs must stay inside the candidate tree.
    """
    pending_path_flag: str | None = None
    for argument in arguments:
        if pending_path_flag is not None:
            if pending_path_flag in {"-include", "-imacros"}:
                require_repo_file(root, argument, f"frontend {pending_path_flag} input")
            else:
                _require_repo_dir(root, argument, pending_path_flag)
            pending_path_flag = None
            continue
        if argument in PATH_FLAGS:
            pending_path_flag = argument
            continue
        matched = False
        for prefix in ("-I", "-iquote", "-isystem", "-idirafter"):
            if argument.startswith(prefix) and len(argument) > len(prefix):
                _require_repo_dir(root, argument[len(prefix):], prefix)
                matched = True
                break
        if matched:
            continue
        if argument.startswith("--sysroot=") or argument.startswith("-isysroot="):
            raise AttestationError("explicit sysroot substitution is not allowed by trusted attestation")
    if pending_path_flag is not None:
        raise AttestationError(f"frontend argument {pending_path_flag} is missing its path")


def _require_repo_dir(root: Path, relative: str, what: str) -> Path:
    if not relative or relative.startswith("/"):
        raise AttestationError(f"frontend {what} path must be relative: {relative!r}")
    lexical = Path(relative)
    if ".." in lexical.parts:
        raise AttestationError(f"frontend {what} path escapes the candidate workspace: {relative!r}")
    path = root / lexical
    if path.is_symlink():
        raise AttestationError(f"frontend {what} path must not be a symlink: {relative}")
    try:
        resolved = path.resolve(strict=True)
        repo = root.resolve(strict=True)
    except OSError as exc:
        raise AttestationError(f"cannot resolve frontend {what} path {relative!r}: {exc}") from exc
    if resolved != repo and repo not in resolved.parents:
        raise AttestationError(f"frontend {what} path resolves outside candidate workspace: {relative}")
    if not resolved.is_dir():
        raise AttestationError(f"frontend {what} path is not a directory: {relative}")
    return resolved


def changed_files(repo: Path, base_sha: str, head_sha: str) -> list[str]:
    process = run(
        ["git", "diff", "--name-only", "--diff-filter=ACMRTUXB", base_sha, head_sha, "--"],
        cwd=repo,
    )
    return sorted(line for line in process.stdout.splitlines() if line)


def is_sensitive(path: str) -> bool:
    return path in SENSITIVE_EXACT or any(path.startswith(prefix) for prefix in SENSITIVE_PREFIXES)


def github_json(url: str, token: str) -> object:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "cand-trusted-attestation",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.load(response)
    except (urllib.error.URLError, json.JSONDecodeError) as exc:
        raise AttestationError(f"cannot query GitHub review state: {exc}") from exc


def has_exact_head_approval(repository: str, pr_number: int, head_sha: str,
                            author: str, token: str,
                            trusted_reviewers: set[str]) -> tuple[bool, list[str]]:
    approved: set[str] = set()
    page = 1
    while True:
        url = (
            f"https://api.github.com/repos/{repository}/pulls/{pr_number}/reviews"
            f"?per_page=100&page={page}"
        )
        payload = github_json(url, token)
        if not isinstance(payload, list):
            raise AttestationError("GitHub reviews response is not a list")
        for review in payload:
            if not isinstance(review, dict):
                continue
            user = review.get("user") or {}
            login = user.get("login") if isinstance(user, dict) else None
            if (
                review.get("state") == "APPROVED"
                and review.get("commit_id") == head_sha
                and isinstance(login, str)
                and login != author
                and login in trusted_reviewers
            ):
                approved.add(login)
        if len(payload) < 100:
            break
        page += 1
    return bool(approved), sorted(approved)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--cand", required=True)
    parser.add_argument("--base-sha", required=True)
    parser.add_argument("--head-sha", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--pr-number", required=True, type=int)
    parser.add_argument("--pr-author", required=True)
    parser.add_argument("--trusted-reviewers", required=True)
    parser.add_argument("--output-dir", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not HEX_SHA_RE.fullmatch(args.base_sha) or not HEX_SHA_RE.fullmatch(args.head_sha):
        raise AttestationError("base/head SHA must be full 40-character lowercase Git object IDs")

    candidate = Path(args.candidate).resolve(strict=True)
    cand = Path(args.cand).resolve(strict=True)
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    evidence_path = output_dir / "evidence.json"
    check_path = output_dir / "agent-check.json"
    verify_path = output_dir / "evidence-verify.json"
    summary_path = output_dir / "attestation-summary.json"

    policy_path = require_repo_file(candidate, "cand-policy.json", "proof policy")
    policy = load_json(policy_path)
    scope = ((policy.get("scope") or {}).get("files"))
    frontend = policy.get("frontend") or {}
    standard = frontend.get("standard")
    frontend_args = frontend.get("arguments")
    pins = ((policy.get("contracts") or {}).get("trusted"))
    if not isinstance(scope, list) or not scope or not all(isinstance(item, str) for item in scope):
        raise AttestationError("cand-policy.json must define a non-empty string scope.files list")
    if standard != "c11" or not isinstance(frontend_args, list) or not all(isinstance(item, str) for item in frontend_args):
        raise AttestationError("trusted attestation currently requires C11 and a string frontend.arguments list")
    if not isinstance(pins, list):
        raise AttestationError("cand-policy.json contracts.trusted must be a list")

    for source in scope:
        require_repo_file(candidate, source, "checked source")
    validate_frontend_paths(candidate, frontend_args)

    contract_path: str | None = None
    if len(pins) > 1:
        raise AttestationError("P0.5 evidence replay currently supports at most one trusted contract bundle")
    if pins:
        pin = pins[0]
        if not isinstance(pin, dict) or not isinstance(pin.get("path"), str):
            raise AttestationError("invalid trusted contract pin")
        contract_path = pin["path"]
        require_repo_file(candidate, contract_path, "trusted contract")

    changed = changed_files(candidate, args.base_sha, args.head_sha)
    sensitive = [path for path in changed if is_sensitive(path)]

    approval_token = os.environ.get("GITHUB_TOKEN", "")
    verifier_env = os.environ.copy()
    # The candidate is untrusted input. Even though P0.5 pins frontend flags,
    # never expose the workflow token to Clang/verifier subprocesses.
    verifier_env.pop("GITHUB_TOKEN", None)
    verifier_env["CAND_TRUSTED_BASE_SHA"] = args.base_sha
    command = [
        str(cand), "check", "--agent", "--base", "origin/main",
        "--policy", "cand-policy.json", "--emit-evidence", str(evidence_path),
    ]
    if contract_path:
        command += ["--contracts", contract_path]
    command += scope + ["--", f"-std={standard}"] + frontend_args
    checked = run(command, cwd=candidate, env=verifier_env, check=False)
    check_path.write_text(checked.stdout, encoding="utf-8")
    if checked.stderr:
        (output_dir / "agent-check.stderr.txt").write_text(checked.stderr, encoding="utf-8")
    try:
        result = json.loads(checked.stdout)
    except json.JSONDecodeError as exc:
        raise AttestationError(f"trusted cand output is not JSON (exit {checked.returncode}): {exc}") from exc
    if not isinstance(result, dict):
        raise AttestationError("trusted cand output is not a JSON object")

    semantic = result.get("semantic_result")
    final_result = result.get("result")
    if semantic != "pass":
        raise AttestationError(f"semantic result is {semantic!r}; trusted acceptance requires semantic PASS")
    if final_result not in {"pass", "review-required"}:
        raise AttestationError(f"agent result is {final_result!r}; proof-policy failure cannot be approved away")
    if not evidence_path.is_file():
        raise AttestationError("trusted cand did not emit evidence")

    requires_review = final_result == "review-required" or bool(sensitive)
    reviewers = {item.strip() for item in args.trusted_reviewers.split(",") if item.strip()}
    approved_by: list[str] = []
    if requires_review:
        if not approval_token or not reviewers:
            raise AttestationError("verification-surface review requires GITHUB_TOKEN and trusted reviewer authority")
        approved, approved_by = has_exact_head_approval(
            args.repository,
            args.pr_number,
            args.head_sha,
            args.pr_author,
            approval_token,
            reviewers,
        )
        if not approved:
            raise AttestationError(
                "exact-head approval from a trusted reviewer is required for verification-surface/review-required changes"
            )

    verified = run(
        [str(cand), "evidence", "verify", str(evidence_path)],
        cwd=candidate,
        env=verifier_env,
        check=False,
    )
    verify_path.write_text(verified.stdout, encoding="utf-8")
    try:
        verify_result = json.loads(verified.stdout)
    except json.JSONDecodeError as exc:
        raise AttestationError(f"evidence verifier output is not JSON: {exc}") from exc
    if verified.returncode != 0 or not isinstance(verify_result, dict) or verify_result.get("result") != "valid":
        raise AttestationError(f"trusted evidence replay failed: {verify_result}")

    summary = {
        "schema": "cand.trusted-attestation/v1",
        "result": "pass",
        "base_sha": args.base_sha,
        "head_sha": args.head_sha,
        "semantic_result": semantic,
        "agent_result": final_result,
        "verification_surface_changed": bool(sensitive),
        "sensitive_files": sensitive,
        "exact_head_review_required": requires_review,
        "approved_by": approved_by,
        "evidence_replay": "valid",
    }
    summary_path.write_text(json.dumps(summary, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AttestationError as exc:
        print(f"trusted attestation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
