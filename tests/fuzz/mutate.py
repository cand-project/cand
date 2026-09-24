#!/usr/bin/env python3
"""Semantic one-operation mutations for known-good C fixtures."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

OPERATORS = {
    "SWAP_LIFETIME_EVENTS": "KNOWN_VIOLATION",
    "INSERT_ALIAS": "KNOWN_VIOLATION",
    "MOVE_FREE_EARLIER": "KNOWN_VIOLATION",
    "MOVE_FREE_LATER": "SAFE",
    "DUPLICATE_MOVE": "KNOWN_VIOLATION",
    "DUPLICATE_DESTROY": "KNOWN_VIOLATION",
    "ESCAPE_BORROW": "UNSUPPORTED",
    "TRANSPORT_VIA_FIELD": "UNSUPPORTED",
    "TRANSPORT_VIA_ARRAY": "UNSUPPORTED",
    "TRANSPORT_VIA_MEMCPY": "UNSUPPORTED",
    "TRANSPORT_VIA_CAST": "UNSUPPORTED",
    "WRAP_IN_LOOP": "UNSUPPORTED",
    "WRAP_IN_BRANCH": "SAFE",
    "CHANGE_DIRECT_CALL_TO_INDIRECT": "UNSUPPORTED",
    # --- operator expansion (C&1 proof plan Phase B) ---
    "LOCAL_ALIAS_DESTROY_USE": "KNOWN_VIOLATION",
    "LOCAL_ALIAS_TRANSFER": "SAFE",
    "VARIADIC_ESCAPE_LIVE": "UNSUPPORTED",
    "VARIADIC_ESCAPE_DEAD": "UNSUPPORTED",
    "SUMMARY_DESTROY_USE": "KNOWN_VIOLATION",
    "SUMMARY_STASH_RETENTION": "UNSUPPORTED",
    "CONDITIONAL_DESTROY_USE": "KNOWN_VIOLATION",
    "LOOP_DESTROY_USE": "KNOWN_VIOLATION",
    "INTERIOR_POINTER_USE": "UNSUPPORTED",
    "STORE_INTO_GLOBAL": "UNSUPPORTED",
    "UNKNOWN_CALL_DEAD_ARG": "UNSUPPORTED",
    "UNKNOWN_POINTER_RETURN": "UNSUPPORTED",
    "INDIRECT_DESTROY_USE": "UNSUPPORTED",
    "REALLOC_TRANSPORT": "UNSUPPORTED",
    "MOVE_AFTER_DESTROY": "KNOWN_VIOLATION",
    "TRANSPORT_VIA_UNION": "UNSUPPORTED",
    "RETURN_ALIAS_USE": "KNOWN_VIOLATION",
    "CONDITIONAL_ALIAS_DESTROY_USE": "KNOWN_VIOLATION",
    "VOLATILE_STORAGE_TRANSPORT": "KNOWN_VIOLATION",
    "SETJMP_ACROSS_DESTROY": "UNSUPPORTED",
    "ATOMIC_STORAGE_TRANSPORT": "UNSUPPORTED",
    # --- parameter-alias operators (milestone #54 / ADR-0028) ---
    # The callee destroys (or must not destroy) its parameter through a
    # local alias; the caller-side consequence is the pinned surface.
    # The conditional and reassigned forms stay fail-closed INCOMPLETE by
    # design (ADR-0027 join / tracked-owner-overwrite), matching the
    # regression fixtures parameter_alias_conditional_destroy_incomplete.c
    # and parameter_alias_reassigned_local_incomplete.c.
    "PARAM_ALIAS_DESTROY_USE": "KNOWN_VIOLATION",
    "PARAM_ALIAS_DESTROY_SAFE": "SAFE",
    "PARAM_ALIAS_CONDITIONAL_DESTROY_USE": "UNSUPPORTED",
    "PARAM_ALIAS_REASSIGNED": "UNSUPPORTED",
    "PARAM_ALIAS_TWO_PARAM_SAFE": "SAFE",
    "PARAM_ALIAS_TWO_PARAM_USE": "KNOWN_VIOLATION",
    # --- reviewed external-declaration operators (milestone #39 / ADR-0029) ---
    # Body-less declarations carrying C& annotations seed summaries only
    # through the fixed strict-workspace review manifest (strict.py); the
    # manifest symbols never change per case, so no case can promote an
    # unreviewed fact into the trusted base. The unreviewed and
    # fact-mismatch forms must stay fail-closed INCOMPLETE.
    "EXTERN_OWNED_RETURN_SAFE": "SAFE",
    "EXTERN_OWNED_RETURN_UAF": "KNOWN_VIOLATION",
    "EXTERN_OWNED_RETURN_DOUBLE_FREE": "KNOWN_VIOLATION",
    "EXTERN_BORROW_LIFETIME_SAFE": "SAFE",
    "EXTERN_BORROW_LIFETIME_UAF": "KNOWN_VIOLATION",
    "EXTERN_UNREVIEWED_NO_MANIFEST": "UNSUPPORTED",
    "EXTERN_FACT_MISMATCH": "UNSUPPORTED",
    # --- milestone #41: bounded produces_out_owner contracts ---
    # These operators run in the feature-enabled strict workspace
    # (PointerOutputWorkspace), never in the plain v1 workspace.
    "POINTER_OUTPUT_C1_SAFE": "SAFE",
    "POINTER_OUTPUT_C2_EMBEDDED_SAFE": "SAFE",
    "POINTER_OUTPUT_C4_DEST_GUARD_SAFE": "SAFE",
    "POINTER_OUTPUT_C1_UAF": "KNOWN_VIOLATION",
    "POINTER_OUTPUT_C1_DOUBLE_FREE": "KNOWN_VIOLATION",
    "POINTER_OUTPUT_UNREFINED_DEREF": "UNSUPPORTED",
    "POINTER_OUTPUT_GUARD_INVERSION": "UNSUPPORTED",
    "POINTER_OUTPUT_VARIADIC_REFUSED": "UNSUPPORTED",
}

REQUIRED_SHAPES = {
    "SWAP_LIFETIME_EVENTS": (r"free\(p\);[\s\S]*cand1_sink\d+ = p->value",),
    "INSERT_ALIAS": (r"void \*alias = p", r"\(\(int \*\)alias\)"),
    "MOVE_FREE_EARLIER": (r"free\(p\);[\s\S]*cand1_sink\d+ = p->value",),
    "MOVE_FREE_LATER": (r"return [^;]+;[\s\S]*free\(p\);",),
    "DUPLICATE_MOVE": (r"CAND_MOVE\(p\);[\s\S]*CAND_MOVE\(p\)", r"p->value"),
    "DUPLICATE_DESTROY": (r"free\(p\);[\s\S]*free\(p\);",),
    "ESCAPE_BORROW": (r"escaped = p",),
    "TRANSPORT_VIA_FIELD": (r"struct Holder", r"holder\.p"),
    "TRANSPORT_VIA_ARRAY": (r"void \*array\[1\]", r"array\[0\]"),
    "TRANSPORT_VIA_MEMCPY": (r"memcpy\(&copy",),
    "TRANSPORT_VIA_CAST": (r"uintptr_t raw", r"p = \(void \*\)raw"),
    "WRAP_IN_LOOP": (r"for \(int i = 0; i < 1; \+\+i\)", r"void \*loop_copy", r"memcpy"),
    "WRAP_IN_BRANCH": (r"if \(p != NULL\) free\(p\)",),
    "CHANGE_DIRECT_CALL_TO_INDIRECT": (r"\(\*destroy\)\(void \*\)", r"destroy\(p\)"),
    "LOCAL_ALIAS_DESTROY_USE": (r"\*q = p;", r"free\(q\);", r"= q->value;"),
    "LOCAL_ALIAS_TRANSFER": (r"\*q = p;", r"p = NULL;", r"free\(q\);"),
    "VARIADIC_ESCAPE_LIVE": (r"cand1_va_sink\(0, p\);",),
    "VARIADIC_ESCAPE_DEAD": (r"free\(p\);[\s\S]*cand1_va_sink\(0, p\);",),
    "SUMMARY_DESTROY_USE": (r"cand1_destroy\(p\);", r"= p->value;"),
    "SUMMARY_STASH_RETENTION": (r"cand1_stash\(p\);",),
    "CONDITIONAL_DESTROY_USE": (r"if \(p->value >= 0\) free\(p\);", r"= p->value;"),
    "LOOP_DESTROY_USE": (r"for \(int i = 0; i < 2; \+\+i\)", r"if \(i == 1\)"),
    "INTERIOR_POINTER_USE": (r"int \*ip = &p->value;", r"= \*ip;"),
    "STORE_INTO_GLOBAL": (r"static CandItem\d+ \*cand1_stash;", r"cand1_stash->value"),
    "UNKNOWN_CALL_DEAD_ARG": (r"free\(p\);[\s\S]*cand1_unknown\(p\);",),
    "UNKNOWN_POINTER_RETURN": (r"cand1_unknown_ret\(void\)", r"u != NULL"),
    "INDIRECT_DESTROY_USE": (r"\(\*destroy\)\(void \*\) = free;", r"destroy\(p\);"),
    "REALLOC_TRANSPORT": (r"p = realloc\(p,",),
    "MOVE_AFTER_DESTROY": (r"free\(p\);[\s\S]*CAND_MOVE\(p\)",),
    "TRANSPORT_VIA_UNION": (r"union Cand1Slot", r"slot\.ptr"),
    "RETURN_ALIAS_USE": (r"cand1_identity\(p\)", r"= r->value;"),
    "CONDITIONAL_ALIAS_DESTROY_USE": (r"if \(p->value >= 0\) \{[\s\S]*free\(q\);[\s\S]*\}",),
    "VOLATILE_STORAGE_TRANSPORT": (r"void \* volatile v = p;",),
    "SETJMP_ACROSS_DESTROY": (r"setjmp\(jb\)", r"longjmp\(jb, 1\)"),
    "ATOMIC_STORAGE_TRANSPORT": (r"_Atomic\(void \*\) slot", r"atomic_store\(&slot, p\)"),
    "PARAM_ALIAS_DESTROY_USE": (r"cand1_alias_destroy\(p\);", r"cand1_alias_destroy\(\w+ \*q\)", r"= p->value;"),
    "PARAM_ALIAS_DESTROY_SAFE": (r"cand1_alias_destroy\(p\);", r"cand1_alias_destroy\(\w+ \*q\)"),
    "PARAM_ALIAS_CONDITIONAL_DESTROY_USE": (r"cand1_cond_alias_destroy\(p, 1\);", r"if \(c\) \{", r"= p->value;"),
    "PARAM_ALIAS_REASSIGNED": (r"cand1_alias_reassigned\(p\);", r"r = malloc", r"= p->value;"),
    "PARAM_ALIAS_TWO_PARAM_SAFE": (r"cand1_two_alias\(p, p2\);", r"= p2->value;"),
    "PARAM_ALIAS_TWO_PARAM_USE": (r"cand1_two_alias\(p, p\);", r"= p->value;"),
    "EXTERN_OWNED_RETURN_SAFE": (r"cand1_extern_create\(void\)", r"cand1_extern_destroy\(p\);"),
    "EXTERN_OWNED_RETURN_UAF": (r"cand1_extern_destroy\(p\);[\s\S]*cand1_sink\d+ = p->value;",),
    "EXTERN_OWNED_RETURN_DOUBLE_FREE": (r"cand1_extern_destroy\(p\);[\s\S]*cand1_extern_destroy\(p\);",),
    "EXTERN_BORROW_LIFETIME_SAFE": (r"cand1_extern_view\(p\);", r"= view->value;[\s\S]*free\(p\);"),
    "EXTERN_BORROW_LIFETIME_UAF": (r"free\(p\);[\s\S]*view->value;",),
    "EXTERN_UNREVIEWED_NO_MANIFEST": (r"cand1_extern_unreviewed_create\(void\)",
                                      r"= cand1_extern_unreviewed_create\(\)"),
    "EXTERN_FACT_MISMATCH": (r"cand1_extern_destroy\(void \*item CAND_BORROW\)",),
    # --- milestone #41: produces_out_owner call-site and guard shapes ---
    "POINTER_OUTPUT_C1_SAFE": (r"if \(cand1_po_status\(&cand1_out\) == 0\)",
                               r"= \*cand1_out;\n\s+free\(cand1_out\);"),
    "POINTER_OUTPUT_C2_EMBEDDED_SAFE": (r"if \(\(cand1_status = cand1_po_status\(&cand1_out\)\) == 0\)",),
    "POINTER_OUTPUT_C4_DEST_GUARD_SAFE": (r"cand1_po_maybe\(&cand1_out\);",
                                          r"if \(cand1_out != NULL\)"),
    "POINTER_OUTPUT_C1_UAF": (r"free\(cand1_out\);\n\s+\w+ = \*cand1_out;",),
    "POINTER_OUTPUT_C1_DOUBLE_FREE": (r"free\(cand1_out\);\n\s+free\(cand1_out\);",),
    "POINTER_OUTPUT_UNREFINED_DEREF": (r"cand1_po_status\(&cand1_out\);\n\s+\w+ = \*cand1_out;",),
    "POINTER_OUTPUT_GUARD_INVERSION": (r"== 0\) return 0;\n\s+\w+ = \*cand1_out;",),
    "POINTER_OUTPUT_VARIADIC_REFUSED": (r"cand1_po_variadic\(&cand1_out, 1\);",),
}


def _facts(source: str) -> tuple[str, str, str]:
    """Extract (item type, sink variable, case function header) from a base case."""
    typ = re.search(r"(\w+) \*p CAND_OWN", source).group(1)
    sink = re.search(r"volatile int (cand1_sink\d+)", source).group(1)
    casefn = re.search(r"int cand1_case_\d+\(void\)", source).group(0)
    return typ, sink, casefn


def _insert_before_case(source: str, casefn: str, helper: str) -> str:
    return source.replace(casefn, helper + "\n\n" + casefn, 1)


def apply(source: str, mutation: str) -> str:
    if mutation not in OPERATORS:
        raise ValueError(f"unknown mutation: {mutation}")
    if mutation == "SWAP_LIFETIME_EVENTS" or mutation == "MOVE_FREE_EARLIER":
        return re.sub(
            r"(?m)^(\s*)(cand1_sink\d+ = p->value;)\n(\s*)free\(p\);$",
            r"\1free(p);\n\3\2", source, count=1,
        )
    if mutation == "MOVE_FREE_LATER":
        return re.sub(
            r"(?m)^(\s*)free\(p\);\n(\s*)(return [^;]+;)$",
            r"\2\3\n\1free(p);", source, count=1,
        )
    if mutation == "DUPLICATE_DESTROY":
        return source.replace("free(p);", "free(p);\n    free(p);", 1)
    if mutation == "DUPLICATE_MOVE":
        sink = re.search(r"cand1_sink\d+", source)
        use = f"{sink.group(0)} = p->value;" if sink else "(void)p;"
        return source.replace("CAND_MOVE(p)", f"CAND_MOVE(p); CAND_MOVE(p); {use}", 1)
    if mutation == "INSERT_ALIAS":
        sink = re.search(r"cand1_sink\d+ = p->value;", source)
        replacement = "void *alias = p;\n    free(p);\n    " + (sink.group(0).replace("p->value", "((int *)alias)[0]") if sink else "return 0;")
        return source.replace("free(p);", replacement, 1)
    if mutation == "ESCAPE_BORROW":
        return source.replace("free(p);", "static void *escaped;\n    escaped = p;\n    free(p);", 1)
    if mutation == "TRANSPORT_VIA_FIELD":
        return source.replace("free(p);", "struct Holder { void *p; } holder = {p};\n    free(p);\n    p = holder.p;", 1)
    if mutation == "TRANSPORT_VIA_ARRAY":
        return source.replace("free(p);", "void *array[1] = {p};\n    free(p);\n    p = array[0];", 1)
    if mutation == "TRANSPORT_VIA_MEMCPY":
        return source.replace("free(p);", "void *copy = NULL;\n    memcpy(&copy, &p, sizeof copy);\n    free(p);\n    p = copy;", 1)
    if mutation == "TRANSPORT_VIA_CAST":
        return source.replace(
            "free(p);", "uintptr_t raw = (uintptr_t)p;\n    free(p);\n    p = (void *)raw;", 1
        )
    if mutation == "WRAP_IN_LOOP":
        return source.replace(
            "free(p);", "for (int i = 0; i < 1; ++i) { void *loop_copy = p; memcpy(&p, &loop_copy, sizeof p); }\n    free(p);", 1
        )
    if mutation == "WRAP_IN_BRANCH":
        return source.replace("free(p);", "if (p != NULL) free(p);", 1)
    if mutation == "CHANGE_DIRECT_CALL_TO_INDIRECT":
        return source.replace("free(p);", "void (*destroy)(void *) = free;\n    destroy(p);", 1)

    typ, sink, casefn = _facts(source)

    if mutation == "LOCAL_ALIAS_DESTROY_USE":
        return source.replace(
            "free(p);", f"{typ} *q = p;\n    free(q);\n    {sink} = q->value;", 1)
    if mutation == "LOCAL_ALIAS_TRANSFER":
        return source.replace(
            "free(p);", f"{typ} *q = p;\n    p = NULL;\n    free(q);", 1)
    if mutation == "VARIADIC_ESCAPE_LIVE":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;",
                           f"{sink} = p->value;\n    cand1_va_sink(0, p);", 1),
            casefn, "static void cand1_va_sink(int fmt, ...) { (void)fmt; }")
    if mutation == "VARIADIC_ESCAPE_DEAD":
        return _insert_before_case(
            source.replace("free(p);", "free(p);\n    cand1_va_sink(0, p);", 1),
            casefn, "static void cand1_va_sink(int fmt, ...) { (void)fmt; }")
    if mutation == "SUMMARY_DESTROY_USE":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"cand1_destroy(p);\n    {sink} = p->value;", 1),
            casefn, f"static void cand1_destroy({typ} *q) {{ free(q); }}")
    if mutation == "SUMMARY_STASH_RETENTION":
        return _insert_before_case(
            source.replace("free(p);", "cand1_stash(p);\n    free(p);", 1),
            casefn, f"static void cand1_stash({typ} *q) {{ static {typ} *kept; kept = q; }}")
    if mutation == "CONDITIONAL_DESTROY_USE":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"if (p->value >= 0) free(p);\n    {sink} = p->value;", 1)
    if mutation == "LOOP_DESTROY_USE":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"for (int i = 0; i < 2; ++i) {{\n        if (i == 1) {sink} = p->value;\n        free(p);\n    }}", 1)
    if mutation == "INTERIOR_POINTER_USE":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"int *ip = &p->value;\n    free(p);\n    {sink} = *ip;", 1)
    if mutation == "STORE_INTO_GLOBAL":
        return source.replace(
            "free(p);",
            f"static {typ} *cand1_stash;\n    cand1_stash = p;\n    free(p);\n    {sink} = cand1_stash->value;", 1)
    if mutation == "UNKNOWN_CALL_DEAD_ARG":
        return _insert_before_case(
            source.replace("free(p);", "free(p);\n    cand1_unknown(p);", 1),
            casefn, "extern int cand1_unknown(void *);")
    if mutation == "UNKNOWN_POINTER_RETURN":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"void *u = cand1_unknown_ret();\n    {sink} = u != NULL;\n    free(p);", 1),
            casefn, "extern void *cand1_unknown_ret(void);")
    if mutation == "INDIRECT_DESTROY_USE":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"void (*destroy)(void *) = free;\n    destroy(p);\n    {sink} = p->value;", 1)
    if mutation == "REALLOC_TRANSPORT":
        return source.replace("free(p);", "p = realloc(p, sizeof *p * 2);\n    free(p);", 1)
    if mutation == "MOVE_AFTER_DESTROY":
        return source.replace(
            "free(p);", f"free(p);\n    {typ} *q CAND_OWN = CAND_MOVE(p);", 1)
    if mutation == "TRANSPORT_VIA_UNION":
        return source.replace(
            "free(p);",
            f"union Cand1Slot {{ {typ} *ptr; unsigned long raw; }} slot;\n    slot.ptr = p;\n    free(p);\n    {sink} = slot.ptr->value;", 1)
    if mutation == "RETURN_ALIAS_USE":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"{typ} *r = cand1_identity(p);\n    free(p);\n    {sink} = r->value;", 1),
            casefn, f"static {typ} *cand1_identity({typ} *q) {{ return q; }}")
    if mutation == "CONDITIONAL_ALIAS_DESTROY_USE":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"if (p->value >= 0) {{ {typ} *q = p; free(q); }}\n    {sink} = p->value;", 1)
    if mutation == "VOLATILE_STORAGE_TRANSPORT":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"void * volatile v = p;\n    free(p);\n    {sink} = (({typ} *)v)->value;", 1)
    if mutation == "SETJMP_ACROSS_DESTROY":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"jmp_buf jb;\n    if (setjmp(jb) == 0) {{ free(p); longjmp(jb, 1); }}\n    {sink} = p->value;", 1)
    if mutation == "ATOMIC_STORAGE_TRANSPORT":
        return source.replace(
            f"{sink} = p->value;\n    free(p);",
            f"_Atomic(void *) slot;\n    atomic_store(&slot, p);\n    free(p);\n    {sink} = (({typ} *)atomic_load(&slot))->value;", 1)

    # --- parameter-alias operators (milestone #54 / ADR-0028) ---
    if mutation == "PARAM_ALIAS_DESTROY_USE":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"cand1_alias_destroy(p);\n    {sink} = p->value;", 1),
            casefn, f"static void cand1_alias_destroy({typ} *q) {{ {typ} *r = q; free(r); }}")
    if mutation == "PARAM_ALIAS_DESTROY_SAFE":
        return _insert_before_case(
            source.replace("free(p);", "cand1_alias_destroy(p);", 1),
            casefn, f"static void cand1_alias_destroy({typ} *q) {{ {typ} *r = q; free(r); }}")
    if mutation == "PARAM_ALIAS_CONDITIONAL_DESTROY_USE":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"cand1_cond_alias_destroy(p, 1);\n    {sink} = p->value;", 1),
            casefn, f"static void cand1_cond_alias_destroy({typ} *q, int c) {{ if (c) {{ {typ} *r = q; free(r); }} }}")
    if mutation == "PARAM_ALIAS_REASSIGNED":
        return _insert_before_case(
            source.replace("free(p);",
                           f"cand1_alias_reassigned(p);\n    {sink} = p->value;", 1),
            casefn, f"static void cand1_alias_reassigned({typ} *q) {{ {typ} *r = q; r = malloc(sizeof *r); free(r); }}")
    if mutation == "PARAM_ALIAS_TWO_PARAM_SAFE":
        return _insert_before_case(
            source.replace("free(p);",
                           f"{{ {typ} *p2 = malloc(sizeof *p2); cand1_two_alias(p, p2); {sink} = p2->value; }}", 1),
            casefn, f"static void cand1_two_alias({typ} *a, {typ} *b) {{ {typ} *r = a; free(r); (void)b; }}")
    if mutation == "PARAM_ALIAS_TWO_PARAM_USE":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"cand1_two_alias(p, p);\n    {sink} = p->value;", 1),
            casefn, f"static void cand1_two_alias({typ} *a, {typ} *b) {{ {typ} *r = a; free(r); (void)b; }}")

    # --- reviewed external-declaration operators (milestone #39 / ADR-0029) ---
    # Fixed helper symbols; the strict-workspace review manifest records
    # exactly these facts and never changes per case.
    extern_decls = (
        '#define CAND_RETURNS_OWN CAND_ANNOTATE("cand:returns_own")\n'
        '#define CAND_DESTROYS CAND_ANNOTATE("cand:destroys")\n'
        "extern CAND_RETURNS_OWN void *cand1_extern_create(void);\n"
        "extern void cand1_extern_destroy(void *item CAND_DESTROYS);\n"
        "extern CAND_RETURNS_BORROW_FROM(0) void *cand1_extern_view(void *base CAND_BORROW);"
    )
    if mutation == "EXTERN_OWNED_RETURN_SAFE":
        return _insert_before_case(
            source.replace(f"{typ} *p CAND_OWN = malloc(sizeof *p);",
                           f"{typ} *p = cand1_extern_create();", 1)
                  .replace("free(p);", "cand1_extern_destroy(p);", 1),
            casefn, extern_decls)
    if mutation == "EXTERN_OWNED_RETURN_UAF":
        return _insert_before_case(
            source.replace(f"{typ} *p CAND_OWN = malloc(sizeof *p);",
                           f"{typ} *p = cand1_extern_create();", 1)
                  .replace(f"{sink} = p->value;\n    free(p);",
                           f"cand1_extern_destroy(p);\n    {sink} = p->value;", 1),
            casefn, extern_decls)
    if mutation == "EXTERN_OWNED_RETURN_DOUBLE_FREE":
        return _insert_before_case(
            source.replace(f"{typ} *p CAND_OWN = malloc(sizeof *p);",
                           f"{typ} *p = cand1_extern_create();", 1)
                  .replace("free(p);", "cand1_extern_destroy(p);\n    cand1_extern_destroy(p);", 1),
            casefn, extern_decls)
    if mutation == "EXTERN_BORROW_LIFETIME_SAFE":
        return _insert_before_case(
            source.replace("free(p);",
                           f"{typ} *view = cand1_extern_view(p);\n    "
                           f"{sink} = view->value;\n    free(p);", 1),
            casefn, extern_decls)
    if mutation == "EXTERN_BORROW_LIFETIME_UAF":
        return _insert_before_case(
            source.replace(f"{sink} = p->value;\n    free(p);",
                           f"{typ} *view = cand1_extern_view(p);\n    free(p);\n    "
                           f"{sink} = view->value;", 1),
            casefn, extern_decls)
    if mutation == "EXTERN_UNREVIEWED_NO_MANIFEST":
        # An annotated declaration that the fixed manifest does not list:
        # candidate-only, must never seed (fail-closed INCOMPLETE).
        unreviewed_decls = (
            '#define CAND_RETURNS_OWN CAND_ANNOTATE("cand:returns_own")\n'
            "extern CAND_RETURNS_OWN void *cand1_extern_unreviewed_create(void);"
        )
        return _insert_before_case(
            source.replace(f"{typ} *p CAND_OWN = malloc(sizeof *p);",
                           f"{typ} *p = cand1_extern_unreviewed_create();", 1),
            casefn, unreviewed_decls)
    if mutation == "EXTERN_FACT_MISMATCH":
        # The declaration's reviewed annotation (borrow) contradicts the
        # fixed manifest fact for the same symbol (destroys): mismatched
        # facts must never seed (fail-closed INCOMPLETE).
        mismatch_decls = (
            '#define CAND_RETURNS_OWN CAND_ANNOTATE("cand:returns_own")\n'
            "extern CAND_RETURNS_OWN void *cand1_extern_create(void);\n"
            "extern void cand1_extern_destroy(void *item CAND_BORROW);"
        )
        return _insert_before_case(
            source.replace(f"{typ} *p CAND_OWN = malloc(sizeof *p);",
                           f"{typ} *p = cand1_extern_create();", 1)
                  .replace("free(p);", "cand1_extern_destroy(p);", 1),
            casefn, mismatch_decls)
    # --- milestone #41: produces_out_owner call-site and guard shapes.
    # The owned-allocation prologue and use/free tail of the safe base
    # are replaced by a pointer-output body; the reviewed contract
    # bundle (strict.PointerOutputWorkspace) supplies the symbol facts.
    if mutation.startswith("POINTER_OUTPUT_"):
        po_decls = (
            "extern int cand1_po_status(int **out);\n"
            "extern int cand1_po_maybe(int **out);\n"
            "extern int cand1_po_variadic(int **out, ...);"
        )
        po_bodies = {
            "POINTER_OUTPUT_C1_SAFE":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    if (cand1_po_status(&cand1_out) == 0) {{\n"
                "        {sink} = *cand1_out;\n"
                "        free(cand1_out);\n"
                "    }}",
            "POINTER_OUTPUT_C2_EMBEDDED_SAFE":
                "    int *cand1_out = NULL;\n"
                "    int cand1_status = 1;\n"
                "    volatile int {sink} = 0;\n"
                "    if ((cand1_status = cand1_po_status(&cand1_out)) == 0) {{\n"
                "        {sink} = *cand1_out;\n"
                "        free(cand1_out);\n"
                "    }}",
            "POINTER_OUTPUT_C4_DEST_GUARD_SAFE":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    cand1_po_maybe(&cand1_out);\n"
                "    if (cand1_out != NULL) {{\n"
                "        {sink} = *cand1_out;\n"
                "        free(cand1_out);\n"
                "    }}",
            "POINTER_OUTPUT_C1_UAF":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    if (cand1_po_status(&cand1_out) == 0) {{\n"
                "        free(cand1_out);\n"
                "        {sink} = *cand1_out;\n"
                "    }}",
            "POINTER_OUTPUT_C1_DOUBLE_FREE":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    if (cand1_po_status(&cand1_out) == 0) {{\n"
                "        {sink} = *cand1_out;\n"
                "        free(cand1_out);\n"
                "        free(cand1_out);\n"
                "    }}",
            "POINTER_OUTPUT_UNREFINED_DEREF":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    cand1_po_status(&cand1_out);\n"
                "    {sink} = *cand1_out;\n"
                "    free(cand1_out);",
            "POINTER_OUTPUT_GUARD_INVERSION":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    if (cand1_po_status(&cand1_out) == 0) return 0;\n"
                "    {sink} = *cand1_out;\n"
                "    free(cand1_out);",
            "POINTER_OUTPUT_VARIADIC_REFUSED":
                "    int *cand1_out = NULL;\n"
                "    volatile int {sink} = 0;\n"
                "    cand1_po_variadic(&cand1_out, 1);\n"
                "    {sink} = 1;\n"
                "    free(cand1_out);",
        }
        body = po_bodies[mutation].format(sink=sink)
        pattern = (
            rf"{typ} \*p CAND_OWN = malloc\(sizeof \*p\);\n"
            rf"    if \(p == NULL\) return 0;\n"
            rf"    p->value = \d+;\n"
            rf"    volatile int {sink} = 0;\n"
            rf"    {sink} = p->value;\n"
            rf"    free\(p\);"
        )
        mutated = re.sub(pattern, body, source, count=1)
        return _insert_before_case(mutated, casefn, po_decls)
    raise AssertionError(mutation)


def validate_mutation(source: str, mutation: str) -> None:
    if any(not re.search(pattern, source, re.MULTILINE) for pattern in REQUIRED_SHAPES[mutation]):
        raise ValueError(f"{mutation}: transformed source lacks its required structural shape")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--mutation", choices=tuple(OPERATORS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = apply(args.base.read_text(encoding="utf-8"), args.mutation)
    args.output.write_text(result, encoding="utf-8")
    metadata = {
        "schema": "cand.fuzz-mutation/v1",
        "base_case": str(args.base),
        "mutation": args.mutation,
        "expected_class": OPERATORS[args.mutation],
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(metadata, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
