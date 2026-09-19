#!/usr/bin/env python3
"""Source-driven C&1-C case plans, rendering, validation, and classification."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import re
from typing import Any

CLASSES = ("SAFE", "KNOWN_VIOLATION", "UNSUPPORTED")
RESULTS = ("pass", "fail", "incomplete")
GENERATOR_VERSION = "cand1-fuzz/v2-source-driven"

# A mechanism is declared only when its source plan inserts the corresponding
# C syntax. Protocol/frontend mechanisms are tested by their own harnesses.
TEMPLATE_FEATURES: dict[str, tuple[str, ...]] = {
    "safe": ("allocation", "use", "destroy"),
    "safe-move": ("allocation", "move", "destroy"),
    "safe-branch": ("allocation", "branch", "use", "destroy"),
    "safe-loop": ("allocation", "loop", "destroy"),
    "safe-switch": ("allocation", "switch", "break", "destroy"),
    "violation": ("allocation", "use", "destroy"),
    "double-free": ("allocation", "double-destroy"),
    "borrow-violation": ("allocation", "shared-borrow", "destroy", "use"),
    "u-struct-field": ("allocation", "struct-field", "memcpy"),
    "u-nested-field": ("allocation", "struct-field", "nested-field", "memcpy"),
    "u-array": ("allocation", "array", "aggregate", "double-destroy"),
    "u-aggregate": ("allocation", "struct-field", "aggregate", "double-destroy"),
    "u-union": ("allocation", "struct-field", "union", "aggregate", "void-pointer",
                "double-destroy"),
    "u-compound-literal": ("allocation", "compound-literal"),
    "u-memcpy": ("allocation", "memcpy"),
    "u-memmove": ("allocation", "memmove"),
    "u-typed-cast": ("allocation", "typed-cast", "integer-pointer"),
    "u-void-pointer": ("allocation", "void-pointer"),
    "u-integer-pointer": ("allocation", "integer-pointer", "typed-cast"),
    "u-pointer-arithmetic": ("allocation", "pointer-arithmetic"),
    "u-interior-pointer": ("allocation", "interior-pointer"),
    "u-global": ("allocation", "global"),
    "u-static": ("allocation", "static"),
    "u-parameter": ("allocation", "parameter"),
    "u-return": ("allocation", "return"),
    "u-out-parameter": ("allocation", "out-parameter"),
    "u-unknown-call": ("allocation", "unknown-call"),
    "u-function-pointer": ("allocation", "function-pointer"),
    "u-callback": ("allocation", "callback"),
    "u-varargs": ("allocation", "varargs"),
    "u-atomics": ("allocation", "atomics"),
    "u-setjmp": ("allocation", "setjmp"),
    "u-longjmp": ("allocation", "setjmp", "longjmp"),
    "u-realloc": ("allocation", "realloc"),
    "u-switch": ("allocation", "switch", "break"),
    "u-goto": ("allocation", "goto", "void-pointer", "memcpy"),
    "u-loop": ("allocation", "loop"),
    "u-nested-loop": ("allocation", "loop", "nested-loop"),
    "u-break": ("allocation", "loop", "break"),
    "u-continue": ("allocation", "loop", "continue"),
    "u-pointer-reassignment": ("allocation", "pointer-reassignment", "typed-cast"),
}

SOURCE_MECHANISMS = tuple(dict.fromkeys(
    feature for features in TEMPLATE_FEATURES.values() for feature in features
))

PATTERNS = {
    "allocation": r"\bmalloc\s*\(",
    "use": r"(?:->\s*value|\*\s*[a-zA-Z_][a-zA-Z0-9_]*)",
    "destroy": r"\bfree\s*\(",
    "move": r"CAND_MOVE\s*\(",
    "double-destroy": r"\bfree\s*\([^\n]+\)[\s\S]*\bfree\s*\(",
    "shared-borrow": r"CAND_BORROW\b",
    "struct-field": r"struct\s+Holder|\.p\b",
    "nested-field": r"struct\s+Nested|\.holder\b",
    "array": r"\bitems?_\d+\s*\[",
    "aggregate": r"\{\s*\.p\s*=|=\s*\{\s*(?:p|owner)\s*\}",
    "union": r"\bunion\s+",
    "compound-literal": r"\(\s*CandItem\d*\s*\)\s*\{",
    "memcpy": r"\bmemcpy\s*\(",
    "memmove": r"\bmemmove\s*\(",
    "typed-cast": r"=\s*\(\s*CandItem\d*\s*\*\s*\)",
    "void-pointer": r"void\s*\*\s*(?:opaque|q|copy)(?:_\d+)?",
    "integer-pointer": r"uintptr_t\s+raw|\(\s*uintptr_t\s*\)",
    "pointer-arithmetic": r"CandItem\d*\s*\*\s*q\s*=\s*p\s*[+-]\s*1",
    "interior-pointer": r"\(\s*char\s*\*\s*\)\s*p",
    "global": r"static\s+CandItem\d*\s*\*\s*global_\d+",
    "static": r"static\s+CandItem\d*\s*\*\s*saved_\d+",
    "parameter": r"CandItem\d*\s*\*\s*parameter_\d+",
    "return": r"return_\d+\s*\(",
    "out-parameter": r"out_\d+\s*\(",
    "unknown-call": r"unknown_\d+\s*\(\s*p",
    "function-pointer": r"\(\s*\*fn_",
    "callback": r"callback_\d+\s*\(\s*p",
    "varargs": r"variadic_\d+\s*\(",
    "atomics": r"atomic_(?:store|load)\s*\(",
    "setjmp": r"\bsetjmp\s*\(",
    "longjmp": r"\blongjmp\s*\(",
    "realloc": r"\brealloc\s*\(",
    "switch": r"\bswitch\s*\(",
    "goto": r"\bgoto\s+",
    "loop": r"\bfor\s*\(",
    "nested-loop": r"\bfor\s*\([\s\S]*\bfor\s*\(",
    "break": r"\bbreak\s*;",
    "continue": r"\bcontinue\s*;",
    "pointer-reassignment": r"\bp\s*=\s*\(",
    "branch": r"\bif\s*\(",
}


@dataclass(frozen=True)
class Case:
    id: str
    seed: int
    case_index: int
    semantic_class: str
    template: str
    mechanisms: tuple[str, ...]
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


def features_for(template: str) -> tuple[str, ...]:
    try:
        # _owned() intentionally contains a null guard, so every rendered case
        # has one real branch in its source plan.
        return tuple(dict.fromkeys(TEMPLATE_FEATURES[template] + ("use", "destroy", "branch")))
    except KeyError as exc:
        raise ValueError(f"unknown source plan: {template}") from exc


def common_source(case_index: int) -> str:
    return f"""#include <stdint.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define CAND_ANNOTATE(value) __attribute__((annotate(value)))
#define CAND_OWN CAND_ANNOTATE(\"cand:own\")
#define CAND_BORROW CAND_ANNOTATE(\"cand:borrow_shared\")
#define CAND_BORROW_MUT CAND_ANNOTATE(\"cand:borrow_mut\")
#define CAND_MOVE(x) (x)
#define CAND_RETURNS_BORROW_FROM(n) CAND_ANNOTATE(\"cand:returns_borrow_from:\" #n)

typedef struct CandItem{case_index} {{ int value; }} CandItem{case_index};
"""


def _owned(n: int, item: str) -> str:
    return f"""    {item} *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) return 0;
    p->value = {n % 17};
    volatile int cand1_sink{n} = 0;
"""


def render(case: Case) -> str:
    n, item = case.case_index, f"CandItem{case.case_index}"
    prefix = common_source(n)
    if case.template == "safe":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    cand1_sink{n} = p->value;
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "safe-move":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    {item} *q CAND_OWN = CAND_MOVE(p);
    q->value++;
    free(q);
    return 0;
}}
"""
    elif case.template == "safe-branch":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    if (p->value >= 0) cand1_sink{n} = p->value;
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "safe-loop":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    for (int i = 0; i < 1; ++i) cand1_sink{n} = p->value;
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "safe-switch":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    switch (p->value) {{ case 0: cand1_sink{n} = p->value; break; default: break; }}
    free(p);
    return cand1_sink{n};
}}
"""
    elif case.template == "violation":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    free(p);
    cand1_sink{n} = p->value;
    return cand1_sink{n};
}}
"""
    elif case.template == "double-free":
        body = f"""int cand1_case_{n}(void) {{
{_owned(n, item)}    free(p);
    free(p);
    return 0;
}}
"""
    elif case.template == "borrow-violation":
        body = f"""CAND_RETURNS_BORROW_FROM(0) static {item} *borrow_{n}({item} *p) {{ return p; }}
int cand1_case_{n}(void) {{
{_owned(n, item)}    {item} *view CAND_BORROW = borrow_{n}(p);
    free(p);
    cand1_sink{n} = view->value;
    return cand1_sink{n};
}}
"""
    elif case.template.startswith("u-"):
        body = _unsupported_body(case.template[2:], n, item)
    else:
        raise ValueError(f"unknown source plan: {case.template}")
    source = prefix + body
    validate_source(case, source)
    return source


def _unsupported_body(feature: str, n: int, item: str) -> str:
    owned = _owned(n, item)
    op = {
        "struct-field": f"    struct Holder_{n} {{ {item} *p; }} holder;\n    holder.p = p;\n    memcpy(&p, &holder.p, sizeof p);\n",
        "nested-field": f"    struct Holder_{n} {{ {item} *p; }};\n    struct Nested_{n} {{ struct Holder_{n} holder; }} nested;\n    nested.holder.p = p;\n    memcpy(&p, &nested.holder.p, sizeof p);\n",
        "array": f"    {item} *items_{n}[1] = {{ p }};\n    free(items_{n}[0]);\n",
        "aggregate": f"    struct Holder_{n} {{ {item} *p; }} holder = {{ .p = p }};\n    free(holder.p);\n",
        "union": f"    union Union_{n} {{ {item} *p; void *opaque; }} value = {{ .p = p }};\n    free(value.p);\n",
        "compound-literal": f"    {item} *compound_{n} = &({item}){{ .value = p->value }};\n    (void)compound_{n};\n",
        "memcpy": f"    {item} *q = NULL;\n    memcpy(&q, &p, sizeof q);\n    (void)q;\n",
        "memmove": f"    {item} *q = NULL;\n    memmove(&q, &p, sizeof q);\n    (void)q;\n",
        "typed-cast": f"    {item} *q = ({item} *)(uintptr_t)p;\n    (void)q;\n",
        "void-pointer": f"    void *opaque_{n} = (void *)p;\n    {item} *q = opaque_{n};\n    (void)q;\n",
        "integer-pointer": f"    uintptr_t raw_{n} = (uintptr_t)p;\n    {item} *q = ({item} *)raw_{n};\n    (void)q;\n",
        "pointer-arithmetic": f"    {item} *q = p + 1;\n    (void)q;\n",
        "interior-pointer": f"    char *q = (char *)p + 1;\n    (void)q;\n",
        "global": f"    global_{n} = p;\n",
        "static": f"    static {item} *saved_{n};\n    saved_{n} = p;\n",
        "parameter": f"    parameter_{n}(p);\n",
        "return": f"    {item} *q = return_{n}(p);\n    (void)q;\n",
        "out-parameter": f"    out_{n}(&p);\n",
        "unknown-call": f"    unknown_{n}(p);\n",
        "function-pointer": f"    void (*fn_{n})({item} *) = unknown_{n};\n    fn_{n}(p);\n",
        "callback": f"    callback_{n}(p);\n",
        "varargs": f"    variadic_{n}(\"%p\", p);\n",
        "atomics": f"    _Atomic({item} *) slot_{n};\n    atomic_store(&slot_{n}, p);\n",
        "setjmp": f"    jmp_buf env_{n};\n    (void)setjmp(env_{n});\n",
        "longjmp": f"    jmp_buf env_{n};\n    if (setjmp(env_{n}) == 0) longjmp(env_{n}, 1);\n",
        "realloc": f"    p = realloc(p, sizeof *p * 2);\n",
        "switch": f"    switch (p->value) {{ case 0: break; default: break; }}\n",
        "goto": f"    void *opaque_{n} = p;\n    goto done_{n};\n done_{n}: memcpy(&p, &opaque_{n}, sizeof p);\n",
        "loop": f"    for (int i = 0; i < 1; ++i) (void)p;\n",
        "nested-loop": f"    for (int i = 0; i < 1; ++i) for (int j = 0; j < 1; ++j) (void)p;\n",
        "break": f"    for (int i = 0; i < 1; ++i) {{ break; }}\n",
        "continue": f"    for (int i = 0; i < 1; ++i) {{ continue; }}\n",
        "pointer-reassignment": f"    p = ({item} *)(void *)p;\n",
    }[feature]
    declarations = {
        "global": f"static {item} *global_{n};\n",
        "parameter": f"static void parameter_{n}({item} *parameter_{n}) {{ (void)parameter_{n}; }}\n",
        "return": f"static {item} *return_{n}({item} *p) {{ return p; }}\n",
        "out-parameter": f"static void out_{n}({item} **out) {{ *out = NULL; }}\n",
        "unknown-call": f"extern void unknown_{n}({item} *);\n",
        "function-pointer": f"extern void unknown_{n}({item} *);\n",
        "callback": f"extern void callback_{n}({item} *);\n",
        "varargs": f"extern void variadic_{n}(const char *, ...);\n",
    }.get(feature, "")
    return declarations + f"int cand1_case_{n}(void) {{\n{owned}{op}    free(p);\n    return 0;\n}}\n"


def detect_mechanisms(source: str) -> tuple[str, ...]:
    body = "\n".join(line for line in source.splitlines() if not line.lstrip().startswith("#define"))
    return tuple(feature for feature in SOURCE_MECHANISMS
                 if feature in PATTERNS and re.search(PATTERNS[feature], body, re.MULTILINE))


def validate_source(case: Case, source: str) -> None:
    planned = features_for(case.template)
    if tuple(case.mechanisms) != planned:
        raise ValueError(f"{case.id}: mechanisms are not source-plan derived")
    missing = [feature for feature in planned if feature not in detect_mechanisms(source)]
    if missing:
        raise ValueError(f"{case.id}: source does not exercise {missing}")


def classify(case: Case, report: dict[str, Any], returncode: int) -> str:
    actual = report.get("result")
    if actual not in RESULTS:
        return "HARNESS_ERROR"
    if case.semantic_class == "SAFE":
        return {"pass": "CORRECT_PASS", "fail": "FALSE_POSITIVE", "incomplete": "COVERAGE_GAP"}[actual]
    if case.semantic_class == "KNOWN_VIOLATION":
        return {"pass": "FALSE_PASS", "fail": "CORRECT_FAIL", "incomplete": "COVERAGE_GAP"}[actual]
    return {"pass": "FALSE_PASS", "fail": "WRONG_FAILURE_CLASS", "incomplete": "CORRECT_INCOMPLETE"}[actual]


def make_case(seed: int, index: int, semantic_class: str, template: str,
              mechanisms: tuple[str, ...] | None = None, mutation: str | None = None,
              base_case: str | None = None) -> Case:
    if semantic_class not in CLASSES:
        raise ValueError(semantic_class)
    planned = features_for(template)
    if mechanisms is not None and tuple(mechanisms) != planned:
        raise ValueError(f"{template}: mechanisms must be source-plan derived")
    return Case(f"cand1-fuzz-{index:06d}", seed, index, semantic_class,
                template, planned, mutation, base_case)
