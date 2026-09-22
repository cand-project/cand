#!/usr/bin/env python3
"""Static policy checks for reviewed contract bundles.

The analyzer's contract loader validates syntax and rejects duplicate
symbols and over-long parameter lists, but it cannot see API *types* or
review policy. This tool enforces the trust-model rules from
docs/contracts/EXTERNAL-API-TRUST-MODEL.md against an embedded,
reviewer-visible table of authoritative signatures (ISO C11 / POSIX.1-2017):

  1. COMPLETENESS: every declared parameter position of the symbol must be
     listed explicitly (unlisted positions default to the fail-closed
     unknown effect and produce spurious obligations).
  2. SCALAR-ONLY no_ownership_effect: a by-value scalar position may be
     no_ownership_effect (the C type system forbids passing a pointer
     there); a pointer position may never be.
  3. POINTER CLASSIFICATION: pointer parameters must be borrow / destroys /
     consumes.
  4. POINTER RETURNS: a pointer-returning symbol must state its return
     ownership; a scalar-returning symbol must not claim one.
  5. EXCLUDED SYMBOLS: symbols with pointer-to-pointer parameters,
     out-owner allocation, varargs state, or conditional ownership must not
     appear in any bundle at all (they stay fail-closed; see the trust
     model, section 3).
  6. PROVENANCE: every contracted symbol must have a provenance section in
     contracts/evidence/<bundle>.md.

Exit status 0 = all checks pass.
"""

from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Authoritative signatures. Parameter types are canonical strings:
#   ptr     = object pointer (const-ness does not matter for ownership class)
#   ptr_out = pointer-to-pointer / pointer written through by the callee
#   scalar  = integer/enum/floating by value
#   va_list = variadic state object
#   void*   = pointer (treated as ptr)
# Return types: ptr / scalar.
SIGNATURES = {
    "malloc": (["scalar"], "ptr"),
    "calloc": (["scalar", "scalar"], "ptr"),
    "free": (["ptr"], "scalar"),
    "realloc": (["ptr", "scalar"], "ptr"),
    "memcpy": (["ptr", "ptr", "scalar"], "ptr"),
    "memmove": (["ptr", "ptr", "scalar"], "ptr"),
    "memset": (["ptr", "scalar", "scalar"], "ptr"),
    "memcmp": (["ptr", "ptr", "scalar"], "scalar"),
    "memchr": (["ptr", "scalar", "scalar"], "ptr"),
    "strlen": (["ptr"], "scalar"),
    "strnlen": (["ptr", "scalar"], "scalar"),
    "strchr": (["ptr", "scalar"], "ptr"),
    "strncmp": (["ptr", "ptr", "scalar"], "scalar"),
    "strncpy": (["ptr", "ptr", "scalar"], "ptr"),
    "strcasecmp": (["ptr", "ptr"], "scalar"),
    "strncasecmp": (["ptr", "ptr", "scalar"], "scalar"),
    "tolower": (["scalar"], "scalar"),
    "toupper": (["scalar"], "scalar"),
    "snprintf": (["ptr", "scalar", "ptr"], "scalar"),
    "vsnprintf": (["ptr", "scalar", "ptr", "va_list"], "scalar"),
    "close": (["scalar"], "scalar"),
    "connect": (["scalar", "ptr", "scalar"], "scalar"),
    "setsockopt": (["scalar", "scalar", "scalar", "ptr", "scalar"], "scalar"),
    "getsockopt": (["scalar", "scalar", "scalar", "ptr", "ptr"], "scalar"),
    "send": (["scalar", "ptr", "scalar", "scalar"], "scalar"),
    "recv": (["scalar", "ptr", "scalar", "scalar"], "scalar"),
}

# Symbols deliberately excluded from every bundle (trust model section 3).
# Any appearance here fails the check.
EXCLUDED = {
    "strerror": "static-storage return",
    "__errno_location": "thread-local storage return",
    "__ctype_b_loc": "thread-local table return",
    "gai_strerror": "static-storage return",
    "strerror_r": "variant-dependent (GNU vs POSIX) return semantics",
    "strtol": "pointer-to-pointer output (endptr); #41 boundary",
    "strtoul": "pointer-to-pointer output (endptr); #41 boundary",
    "getaddrinfo": "out-owner allocation; SPEC-0003 boundary",
    "freeaddrinfo": "out-owner family destruction; SPEC-0003 boundary",
    "accept": "pointer-to-pointer output; #41 boundary",
    "fcntl": "variadic third argument",
    "va_start": "variadic state object",
    "va_end": "variadic state object",
    "va_copy": "variadic state object",
    "__builtin_va_start": "variadic state object",
    "__builtin_va_end": "variadic state object",
    "__builtin_va_copy": "variadic state object",
    "reallocarray": "realloc-like lifetime replacement",
    "strdup": "new allocation (allocator family; separate review)",
    "strndup": "new allocation (allocator family; separate review)",
}

POINTER_EFFECTS = {"borrow", "destroys", "consumes"}


def parse_bundle(path):
    lines = open(path).read().splitlines()
    name = None
    symbols_start = None
    for i, line in enumerate(lines):
        m = re.match(r"^name:\s*(\S+)", line)
        if m and name is None:
            name = m.group(1)
        if line.startswith("symbols:"):
            symbols_start = i
            break
    if name is None or symbols_start is None:
        raise SystemExit(f"{path}: malformed bundle")
    entries = []
    current = None
    for line in lines[symbols_start + 1:]:
        m = re.match(r"^  - symbol:\s*(\S+)\s*$", line)
        if m:
            current = {"symbol": m.group(1), "params": {}, "returns": False}
            entries.append(current)
            continue
        if current is None:
            continue
        pm = re.match(r"^      - index:\s*(\d+)\s*$", line)
        if pm:
            current["pending_index"] = int(pm.group(1))
            continue
        em = re.match(r"^        effect:\s*(\S+)\s*$", line)
        if em and "pending_index" in current:
            current["params"][current.pop("pending_index")] = em.group(1)
            continue
        if re.match(r"^    returns:\s*$", line):
            current["returns"] = True
    return name, entries


def check_bundle(path, errors):
    bundle_name, entries = parse_bundle(path)
    evidence_path = os.path.join(
        ROOT, "contracts", "evidence",
        os.path.basename(path).replace(".yaml", ".md"))
    evidence = open(evidence_path).read() if os.path.exists(evidence_path) else ""
    for entry in entries:
        sym = entry["symbol"]
        where = f"{os.path.relpath(path, ROOT)}:{sym}"
        if sym in EXCLUDED:
            errors.append(f"{where}: excluded symbol ({EXCLUDED[sym]}) must not be contracted")
            continue
        if sym not in SIGNATURES:
            errors.append(f"{where}: no authoritative signature in the checker table; "
                          f"add one (with review) before contracting")
            continue
        if f"## {sym}" not in evidence:
            errors.append(f"{where}: no provenance section in "
                          f"{os.path.relpath(evidence_path, ROOT)}")
        params, ret = SIGNATURES[sym]
        listed = entry["params"]
        for index, kind in enumerate(params):
            if index not in listed:
                if kind == "va_list":
                    continue  # deliberately unlisted: fail-closed unknown
                errors.append(f"{where}: param {index} ({kind}) not listed "
                              f"(completeness rule)")
                continue
            effect = listed[index]
            if kind in ("ptr",):
                if effect not in POINTER_EFFECTS:
                    errors.append(f"{where}: pointer param {index} has effect "
                                  f"{effect!r}; must be one of {sorted(POINTER_EFFECTS)}")
            elif kind in ("scalar",):
                if effect != "no_ownership_effect":
                    errors.append(f"{where}: scalar param {index} has effect "
                                  f"{effect!r}; only no_ownership_effect is sound")
            elif kind == "va_list":
                errors.append(f"{where}: va_list param {index} must stay unlisted")
        for index in sorted(listed):
            if index >= len(params):
                errors.append(f"{where}: param {index} beyond the declared "
                              f"signature ({len(params)} params)")
        if ret == "ptr" and not entry["returns"]:
            errors.append(f"{where}: pointer return without a returns: section")
        if ret == "scalar" and entry["returns"]:
            errors.append(f"{where}: scalar return must not claim a returns: section")


def main():
    errors = []
    bundle_dir = os.path.join(ROOT, "contracts", "bundles")
    bundles = [os.path.join(ROOT, "contracts", "libc.yaml")]
    bundles += sorted(os.path.join(bundle_dir, f)
                      for f in os.listdir(bundle_dir) if f.endswith(".yaml"))
    for path in bundles:
        check_bundle(path, errors)
    if errors:
        for e in errors:
            print(f"bundle policy error: {e}", file=sys.stderr)
        return 1
    print(f"bundle policy: OK ({len(bundles)} bundles)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
