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

PATH_FLAGS = {"-I", "-iquote", "-isystem", "-idirafter", "-include", "-imacros"}
FORBIDDEN_FRONTEND_FLAGS = {
    "-Xclang", "-load", "-fplugin", "-fplugin-file", "-fmodule-map-file",
    "-fmodule-file", "-fmodules-cache-path", "-resource-dir", "-working-directory", "-isysroot",
    "-fpass-plugin", "-load-pass-plugin", "-mllvm", "-include-pch", "-fpch-preprocess",
    "--sysroot", "-target", "--target",
}


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
        if argument.startswith("@") or argument in FORBIDDEN_FRONTEND_FLAGS:
            raise AttestationError(f"frontend argument is not permitted in trusted attestation: {argument}")
        if any(argument.startswith(flag + "=") for flag in FORBIDDEN_FRONTEND_FLAGS):
            raise AttestationError(f"frontend argument is not permitted in trusted attestation: {argument}")
        if argument.startswith("-fmodule") or argument.startswith("-fpass-plugin") or argument.startswith("-load-pass-plugin"):
            raise AttestationError(f"frontend argument is not permitted in trusted attestation: {argument}")
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
        for prefix in ("-include", "-imacros"):
            if argument.startswith(prefix) and len(argument) > len(prefix):
                require_repo_file(root, argument[len(prefix):], f"frontend {prefix} input")
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
        ["git", "diff", "--name-only", "-z", base_sha, head_sha, "--"],
        cwd=repo,
    )
    return sorted(path for path in process.stdout.split("\0") if path)


def load_surface(path: Path) -> tuple[set[str], tuple[str, ...], str]:
    surface = load_json(path)
    if surface.get("schema") != "cand.verifier-surface/v1":
        raise AttestationError("invalid verifier-surface schema")
    exact = surface.get("exact")
    prefixes = surface.get("prefixes")
    unknown_policy = surface.get("unknown_path_policy")
    if (not isinstance(exact, list) or not all(isinstance(item, str) for item in exact) or
            not isinstance(prefixes, list) or not all(isinstance(item, str) for item in prefixes) or
            unknown_policy != "review_required"):
        raise AttestationError("invalid verifier-surface manifest")
    return set(exact), tuple(prefixes), unknown_policy


def surface_class(path: str, exact: set[str], prefixes: tuple[str, ...]) -> str:
    if path in exact or any(path.startswith(prefix) for prefix in prefixes):
        return "sensitive"
    return "unknown"


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
    # GitHub returns review history, not only effective review state. Keep the
    # latest exact-head review from each trusted reviewer so an old APPROVED
    # record cannot survive a later CHANGES_REQUESTED/DISMISSED review.
    latest: dict[str, tuple[int, str]] = {}
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
            if not isinstance(review, dict) or review.get("commit_id") != head_sha:
                continue
            user = review.get("user") or {}
            login = user.get("login") if isinstance(user, dict) else None
            review_id = review.get("id")
            state = review.get("state")
            if (
                isinstance(login, str)
                and login != author
                and login in trusted_reviewers
                and isinstance(review_id, int)
                and isinstance(state, str)
            ):
                previous = latest.get(login)
                if previous is None or review_id > previous[0]:
                    latest[login] = (review_id, state)
        if len(payload) < 100:
            break
        page += 1
    approved = sorted(login for login, (_, state) in latest.items() if state == "APPROVED")
    return bool(approved), approved


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
    surface_path = Path(__file__).with_name("verifier-surface.json")
    exact_surface, surface_prefixes, _ = load_surface(surface_path)
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
    scoped = set(scope)
    sensitive = [path for path in changed
                 if surface_class(path, exact_surface, surface_prefixes) == "sensitive"]
    ambiguous = [path for path in changed
                 if surface_class(path, exact_surface, surface_prefixes) == "unknown" and path not in scoped]
    # A changed production C source that is not in the declared proof scope is
    # not "reviewable PASS" — it was never analyzed.  Tests are verifier
    # surface and follow the exact-head review path instead.
    unscoped_c = [
        path for path in changed
        if path.endswith(".c") and not path.startswith("tests/") and path not in scoped
    ]
    if unscoped_c:
        raise AttestationError(
            "changed C source is outside cand-policy.json scope.files: " + ", ".join(unscoped_c)
        )

    approval_token = os.environ.get("GITHUB_TOKEN", "")
    verifier_env = os.environ.copy()
    # The candidate is untrusted input. Even though P0.5 pins frontend flags,
    # never expose the workflow token to Clang/verifier subprocesses.
    verifier_env.pop("GITHUB_TOKEN", None)
    for variable in (
        "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
        "COMPILER_PATH", "GCC_EXEC_PREFIX", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET",
        "CFLAGS", "CPPFLAGS", "CXXFLAGS", "LDFLAGS", "LD_LIBRARY_PATH", "LIBRARY_PATH",
        "LD_PRELOAD", "DYLD_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES", "CLANG_CONFIG_FILE",
        "LLVM_CONFIG", "LLVM_DIR", "Clang_DIR", "BASH_ENV", "ENV",
    ):
        verifier_env.pop(variable, None)
    verifier_env["PATH"] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
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
    evidence = load_json(evidence_path)
    cand_identity = evidence.get("cand") or {}
    source_identity = evidence.get("source") or {}
    verification = evidence.get("verification") or {}
    if (cand_identity.get("verifier_source_commit") != args.base_sha or
            source_identity.get("commit") != args.head_sha or
            verification.get("trusted_base_sha") != args.base_sha):
        raise AttestationError("evidence provenance does not match protected base and PR head")

    requires_review = final_result == "review-required" or bool(sensitive) or bool(ambiguous)
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
        "verification_surface_changed": bool(sensitive) or bool(ambiguous),
        "sensitive_files": sensitive,
        "ambiguous_files": ambiguous,
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
