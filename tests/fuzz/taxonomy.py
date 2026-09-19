#!/usr/bin/env python3
"""Shared C&1-C taxonomy, rendering, and result classification."""

from __future__ import annotations

from dataclasses import dataclass, asdict
from typing import Any

CLASSES = ("SAFE", "KNOWN_VIOLATION", "UNSUPPORTED")
RESULTS = ("pass", "fail", "incomplete")
GENERATOR_VERSION = "cand1-fuzz/v1"

MECHANISMS = (
    "allocation", "destroy", "use", "move", "double-move", "double-destroy",
    "alias", "pointer-reassignment", "shared-borrow", "mutable-borrow",
    "borrow-escape", "borrowed-return", "branch", "switch", "goto", "early-return",
    "loop", "nested-loop", "break", "continue", "loop-allocation",
    "repeated-allocation-site", "loop-carried-alias", "struct-field", "nested-field",
    "array", "aggregate", "union", "compound-literal", "memcpy", "memmove",
    "typed-cast", "void-pointer", "integer-pointer", "pointer-arithmetic",
    "interior-pointer", "global", "static", "parameter", "return", "out-parameter",
    "unknown-call", "function-pointer", "callback", "cross-tu", "varargs", "atomics",
    "setjmp", "longjmp", "realloc", "contracts", "protocol", "frontend-arguments",
)


@dataclass(frozen=True)
class Case:
    id: str
    seed: int
    case_index: int
    semantic_class: str
    mechanisms: tuple[str, ...]
    template: str
    mutation: str | None = None
    base_case: str | None = None

    def manifest(self) -> dict[str, Any]:
        value = asdict(self)
        value["mechanisms"] = list(self.mechanisms)
        value["expected"] = {"class": self.semantic_class}
        if self.mutation is None:
            value.pop("mutation")
        if self.base_case is None:
            value.pop("base_case")
        return value


def common_source(case_index: int) -> str:
    return f"""#include <cand/cand.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct CandItem{case_index} {{ int value; }} CandItem{case_index};
volatile int cand1_sink{case_index};
"""


def render(case: Case) -> str:
    n = case.case_index
    item = f"CandItem{n}"
    prefix = common_source(n)
    if case.template == "safe":
        body = f"""int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) return 0;
    p->value = {n % 17};
    cand1_sink{n} = p->value;
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "move":
        body = f"""int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) return 0;
    {item} *q CAND_OWN = CAND_MOVE(p);
    q->value = {n % 17};
    free(q);
    return 0;
}}
"""
    elif case.template == "violation":
        body = f"""int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) return 0;
    p->value = {n % 17};
    free(p);
    cand1_sink{n} = p->value;
    return cand1_sink{n};
}}
"""
    elif case.template == "double-free":
        body = f"""int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) return 0;
    free(p);
    free(p);
    return 0;
}}
"""
    elif case.template == "borrow-violation":
        body = f"""static {item} *cand1_borrow_{n}({item} *p) {{ return p; }}
int cand1_case_{n}(void) {{
    {item} *owner CAND_OWN = malloc(sizeof *owner);
    if (owner == NULL) return 0;
    {item} *view CAND_BORROW = cand1_borrow_{n}(owner);
    free(owner);
    cand1_sink{n} = view->value;
    return cand1_sink{n};
}}
"""
    elif case.template == "unsupported":
        body = f"""int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    {item} *q = NULL;
    if (p == NULL) return 0;
    memcpy(&q, &p, sizeof q);
    cand1_sink{n} = q->value;
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "unsupported-wrapper":
        body = f"""static void cand1_copy_{n}(void *dst, const void *src) {{ memcpy(dst, src, sizeof(void *)); }}
int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    {item} *q = NULL;
    if (p == NULL) return 0;
    cand1_copy_{n}(&q, &p);
    cand1_sink{n} = q->value;
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "loop":
        body = f"""int cand1_case_{n}(void) {{
    {item} *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) return 0;
    for (int i = 0; i < 2; ++i) {{
        if (i == 0) free(p);
    }}
    return p->value;
}}
"""
    else:
        raise ValueError(f"unknown template: {case.template}")
    return prefix + body


def classify(case: Case, report: dict[str, Any], returncode: int) -> str:
    """Return the differential result bucket for one analyzer report."""
    actual = report.get("result")
    if actual not in RESULTS:
        return "HARNESS_ERROR"
    expected = case.semantic_class
    if expected == "KNOWN_VIOLATION" and actual == "pass":
        return "FALSE_PASS"
    if expected == "UNSUPPORTED" and actual == "pass":
        return "FALSE_PASS"
    if expected == "SAFE":
        if actual == "pass":
            return "CORRECT_PASS"
        if actual == "incomplete":
            return "CORRECT_INCOMPLETE"
        return "FALSE_POSITIVE"
    if expected == "KNOWN_VIOLATION" and actual == "fail":
        return "CORRECT_FAIL"
    if expected == "UNSUPPORTED" and actual == "incomplete":
        return "CORRECT_INCOMPLETE"
    return "WRONG_FAILURE_CLASS"


def make_case(seed: int, index: int, semantic_class: str, template: str,
              mechanisms: tuple[str, ...], mutation: str | None = None,
              base_case: str | None = None) -> Case:
    if semantic_class not in CLASSES:
        raise ValueError(semantic_class)
    return Case(
        id=f"cand1-fuzz-{index:06d}", seed=seed, case_index=index,
        semantic_class=semantic_class, mechanisms=mechanisms, template=template,
        mutation=mutation, base_case=base_case,
    )
