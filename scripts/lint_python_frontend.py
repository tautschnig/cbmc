#!/usr/bin/env python3
"""Soundness lints for the Python frontend (src/python).

Two false-proof patterns recurred across the 2026-07 campaigns; both were
found by reading invariants, not by fuzzing, so this lint institutionalises
them as a fast static gate:

1. RAW-STRING-INTO-KEY-ARRAY: pushing a raw ``python_string_literal(...)`` into
   a dict *keys* array bypasses ``coerce_element``. Under
   ``--python-smt-strings`` the keys-array element is a string-id HANDLE
   (bv64), so a raw string is ill-typed and aborts simplify/symex (four real
   bugs: the method-/free-function kwargs packing, ``dict(a=1)``, the
   sentinel-dict builders). Key stores MUST route through ``coerce_element``
   (or ``box_string_for_storage`` / ``string_to_handle``).

2. ENGAGED-OPTIONAL-NIL: a ``try_*`` helper returning ``std::optional<exprt>``
   whose caller uses ``if(auto r = try_...(...)) return std::move(*r);`` must
   NEVER ``return nil_exprt{}`` as its value -- an engaged optional holding a
   nil expr is treated as "handled", so the enclosing statement is silently
   DROPPED (a vacuity false proof: the min/max nil tail, dict.fromkeys empty
   expr). Such helpers return ``std::nullopt`` (unhandled) or a typed
   ``side_effect_expr_nondett`` (a real value), never a bare nil.

Exit non-zero on any finding. Reviewed-safe sites are allow-listed inline.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src" / "python"
ALLOW = set()  # (relpath, code) pairs reviewed and known safe
findings = []


def check_raw_string_into_key_array(path, lines):
    for i, ln in enumerate(lines):
        if ".push_back(" not in ln or "key" not in ln.lower():
            continue
        window = " ".join(lines[i : i + 3])
        if "python_string_literal(" not in window:
            continue
        if any(h in window for h in ("coerce_element(", "box_string_for_storage(",
                                     "string_to_handle(")):
            continue
        findings.append((path, i + 1,
                         "raw string literal pushed into a key array without "
                         "coerce_element", ln.strip()))


def check_engaged_optional_nil(path, text):
    for m in re.finditer(
        r"std::optional<exprt>\s+\w+::(try_\w+)\([^;{]*?\)\s*(?:const\s*)?\{",
        text):
        name = m.group(1)
        start = m.end()
        depth, j = 1, m.end()
        while j < len(text) and depth:
            depth += (text[j] == "{") - (text[j] == "}")
            j += 1
        body = text[start:j]
        for bm in re.finditer(r"return\s+nil_exprt\{\}\s*;", body):
            ln = text[: start + bm.start()].count("\n") + 1
            findings.append((path, ln,
                             f"return nil_exprt{{}} in engaged-optional helper "
                             f"'{name}' (drops the enclosing statement)",
                             "return nil_exprt{};"))


def main():
    for cpp in sorted(SRC.glob("*.cpp")):
        text = cpp.read_text(errors="replace")
        rel = str(cpp.relative_to(SRC.parent.parent))
        check_raw_string_into_key_array(rel, text.splitlines())
        check_engaged_optional_nil(rel, text)
    real = [f for f in findings if (f[0], f[3]) not in ALLOW]
    for path, ln, msg, code in real:
        print(f"{path}:{ln}: SOUNDNESS-LINT: {msg}\n    {code}")
    if real:
        print(f"\n{len(real)} finding(s). See scripts/lint_python_frontend.py.")
        return 1
    print("python-frontend soundness lint: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
