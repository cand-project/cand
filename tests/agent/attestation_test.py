#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("attest", ROOT / ".github/trusted/attest.py")
attest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(attest)

def rejected(root, args):
    try:
        attest.validate_frontend_paths(root, args)
    except attest.AttestationError:
        return
    raise AssertionError(f"frontend input unexpectedly accepted: {args}")

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "include").mkdir()
    (root / "header.h").write_text("\n", encoding="utf-8")
    attest.validate_frontend_paths(root, ["-I", "include", "-include", "header.h"])
    rejected(root, ["@args.rsp"])
    rejected(root, ["-Xclang", "-load"])
    rejected(root, ["-isysroot", "/tmp"])
    rejected(root, ["-I", "/tmp"])
    rejected(root, ["-include", "../outside.h"])

exact, prefixes, unknown = attest.load_surface(ROOT / ".github/trusted/verifier-surface.json")
assert unknown == "review_required"
assert attest.surface_class("src/new-analysis.cpp", exact, prefixes) == "sensitive"
assert attest.surface_class("include/cand/new.h", exact, prefixes) == "sensitive"
assert attest.surface_class("future/verifier/plugin.cpp", exact, prefixes) == "unknown"
print("trusted attestation input and manifest tests passed")
