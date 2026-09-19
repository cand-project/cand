#!/usr/bin/env python3
"""Small fail-closed protocol/frontend corpus; it never grants proof authority."""

from __future__ import annotations

import json
from pathlib import PurePosixPath


def validate_payload(text: str) -> bool:
    """Return whether a payload is safe to consider for non-authoritative testing."""
    def reject_duplicates(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError("duplicate JSON key")
            value[key] = item
        return value

    try:
        value = json.loads(text, object_pairs_hook=reject_duplicates)
    except (json.JSONDecodeError, ValueError):
        return False
    if not isinstance(value, dict) or value.get("schema") != "cand.check/v1":
        return False
    if value.get("result") not in {"pass", "fail", "incomplete"}:
        return False
    if any(isinstance(key, str) and key.startswith("__") for key in value):
        return False
    return True


def validate_path(path: str) -> bool:
    value = PurePosixPath(path)
    return not value.is_absolute() and ".." not in value.parts and "\x00" not in path


def validate_frontend_args(args: list[str]) -> bool:
    """Accept only arguments whose paths can be bound into evidence."""
    value_flags = {"-I", "-isystem", "-include", "-imacros", "--sysroot"}
    for index, arg in enumerate(args):
        if arg.startswith("@") or arg in {"-plugin", "-Xclang"}:
            return False
        if arg in value_flags:
            if index + 1 == len(args) or not validate_path(args[index + 1]):
                return False
        elif any(arg.startswith(flag) for flag in ("-I", "-isystem", "-include", "-imacros", "--sysroot")):
            path = arg.split("=", 1)[-1]
            if not validate_path(path):
                return False
    return True


def run_protocol_corpus() -> dict:
    valid = json.dumps({"schema": "cand.check/v1", "result": "incomplete"})
    cases = [
        "", "{", "[]", "{\"schema\":\"cand.check/v1\"}",
        "{\"schema\":\"cand.check/v1\",\"result\":\"pass\",\"__trust\":true}",
        "{\"schema\":\"wrong\",\"result\":\"pass\"}",
        "{\"schema\":\"cand.check/v1\",\"result\":\"forged\"}",
        valid,
    ]
    path_cases = ["tests/a.c", "../a.c", "/tmp/a.c", "a\x00.c", "tests/../a.c"]
    accepted = sum(validate_payload(case) for case in cases)
    safe_paths = sum(validate_path(case) for case in path_cases)
    # A duplicate-key payload is explicitly not authoritative even when Python's
    # JSON parser keeps the last value; this is tested as a separate attack class.
    duplicate = '{"schema":"cand.check/v1","result":"fail","result":"pass"}'
    frontend_cases = [
        ["-std=c11", "-I", "include"], ["-isystem", "sys"], ["-include", "../secret.h"],
        ["-imacros=/tmp/macros.h"], ["--sysroot", "/"], ["@response.txt"],
        ["-plugin", "untrusted"], ["-DVALUE=1"], ["-Iinclude"], ["--target=x86_64"],
    ]
    frontend_bypasses = sum(validate_frontend_args(case) and case in frontend_cases[2:7] for case in frontend_cases)
    authority_bypasses = int(validate_payload(duplicate)) + frontend_bypasses
    return {
        "cases": len(cases) + len(path_cases) + 1 + len(frontend_cases),
        "accepted_well_formed": accepted,
        "safe_paths": safe_paths,
        "frontend_cases": len(frontend_cases),
        "frontend_authority_bypasses": frontend_bypasses,
        "authority_bypasses": authority_bypasses,
    }
