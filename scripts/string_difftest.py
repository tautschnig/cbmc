#!/usr/bin/env python3
"""
Differential tester for the string support of cbmc (Python) and jbmc (Java).

Generates small expressions that apply one or two string functions to
inputs, computes their values with the real runtime (CPython, or the JVM),
and checks with cbmc/jbmc, for every selected back end, that

  * the value is proved (`assert r == <value>` is SUCCESS), and
  * any other value is refuted (`assert r != <value>` is FAILURE).

The inputs are symbolic (nondet) strings and integers pinned to concrete
values by a condition, so the string solver, not constant folding, has to
compute the result. An expression that raises an exception is checked the
same way: the code after it is unreachable, and its handler is reachable.

Each case gets one of these verdicts per back end:

  ok          both checks pass
  unsound     the refutation passed (`r != <value>` was proved); this is
              a bug: the tool proved something false
  imprecise   the value was not proved, but nothing false was proved
  error       VERIFICATION ERROR, a crash, or a conversion error
  timeout     the time limit was reached

Usage:
    scripts/string_difftest.py --lang python --count 200 --out /tmp/dt
    scripts/string_difftest.py --lang java --count 200 --out /tmp/dt-java
    scripts/string_difftest.py --lang python --backends native-cvc5 \\
        --seed 7 --count 50 --out /tmp/dt

All runs bound both memory (ulimit -v) and time (timeout). The cases that
are not ok are kept in the output directory with the tool's output; the
summary lists them grouped by operation.
"""

import argparse
import collections
import concurrent.futures
import json
import os
import random
import re
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Characters of the generated strings, with their weights.
ALPHABET = [
    ("a", 30), ("b", 20), ("A", 6), ("B", 3), (" ", 6), ("1", 5), ("0", 3),
    ("-", 3), ("+", 2), ("_", 2), ("\t", 1), ("é", 2), (".", 2),
]


def random_string(rng, max_len=4):
    chars, weights = zip(*ALPHABET)
    n = rng.choice([0, 1, 1, 2, 2, 2, 3, 3, 4][: max_len + 5])
    n = min(n, max_len)
    return "".join(rng.choices(chars, weights, k=n))


# ---------------------------------------------------------------------------
# Operations. Each is (name, template, argument types, result type).
# Types: str, int (any position), small (a count or width, 0..8),
# char (a one-character string), code (a code point).
# ---------------------------------------------------------------------------

PY_OPS = [
    # strings
    ("concat", "({0} + {1})", ["str", "str"], "str"),
    ("repeat", "({0} * {1})", ["str", "small"], "str"),
    ("index", "{0}[{1}]", ["str", "int"], "str"),
    ("slice", "{0}[{1}:{2}]", ["str", "int", "int"], "str"),
    ("slice-from", "{0}[{1}:]", ["str", "int"], "str"),
    ("slice-to", "{0}[:{1}]", ["str", "int"], "str"),
    ("reverse", "{0}[::-1]", ["str"], "str"),
    ("replace", "{0}.replace({1}, {2})", ["str", "str", "str"], "str"),
    ("replace-count", "{0}.replace({1}, {2}, {3})",
     ["str", "str", "str", "small"], "str"),
    ("upper", "{0}.upper()", ["str"], "str"),
    ("lower", "{0}.lower()", ["str"], "str"),
    ("capitalize", "{0}.capitalize()", ["str"], "str"),
    ("title", "{0}.title()", ["str"], "str"),
    ("swapcase", "{0}.swapcase()", ["str"], "str"),
    ("casefold", "{0}.casefold()", ["str"], "str"),
    ("strip", "{0}.strip()", ["str"], "str"),
    ("lstrip", "{0}.lstrip()", ["str"], "str"),
    ("rstrip", "{0}.rstrip()", ["str"], "str"),
    ("strip-chars", "{0}.strip({1})", ["str", "str"], "str"),
    ("zfill", "{0}.zfill({1})", ["str", "small"], "str"),
    ("center", "{0}.center({1})", ["str", "small"], "str"),
    ("ljust", "{0}.ljust({1})", ["str", "small"], "str"),
    ("rjust", "{0}.rjust({1})", ["str", "small"], "str"),
    ("removeprefix", "{0}.removeprefix({1})", ["str", "str"], "str"),
    ("removesuffix", "{0}.removesuffix({1})", ["str", "str"], "str"),
    ("join", "{0}.join([{1}, {2}])", ["str", "str", "str"], "str"),
    ("split-item", "{0}.split({1})[{2}]", ["str", "str", "int"], "str"),
    ("partition-item", "{0}.partition({1})[{2}]", ["str", "str", "part"],
     "str"),
    ("str-int", "str({0})", ["int"], "str"),
    ("chr", "chr({0})", ["code"], "str"),
    ("fstring", "f'{{{0}}}:{{{1}}}'", ["str", "int"], "str"),
    # integers
    ("len", "len({0})", ["str"], "int"),
    ("find", "{0}.find({1})", ["str", "str"], "int"),
    ("find-start", "{0}.find({1}, {2})", ["str", "str", "int"], "int"),
    ("find-range", "{0}.find({1}, {2}, {3})", ["str", "str", "int", "int"],
     "int"),
    ("rfind", "{0}.rfind({1})", ["str", "str"], "int"),
    ("rfind-start", "{0}.rfind({1}, {2})", ["str", "str", "int"], "int"),
    ("index-of", "{0}.index({1})", ["str", "str"], "int"),
    ("rindex", "{0}.rindex({1})", ["str", "str"], "int"),
    ("count", "{0}.count({1})", ["str", "str"], "int"),
    ("count-start", "{0}.count({1}, {2})", ["str", "str", "int"], "int"),
    ("ord", "ord({0})", ["str"], "int"),
    ("int", "int({0})", ["str"], "int"),
    ("split-len", "len({0}.split({1}))", ["str", "str"], "int"),
    ("split-ws-len", "len({0}.split())", ["str"], "int"),
    # Booleans
    ("startswith", "{0}.startswith({1})", ["str", "str"], "bool"),
    ("startswith-start", "{0}.startswith({1}, {2})", ["str", "str", "int"],
     "bool"),
    ("endswith", "{0}.endswith({1})", ["str", "str"], "bool"),
    ("endswith-range", "{0}.endswith({1}, {2}, {3})",
     ["str", "str", "int", "int"], "bool"),
    ("in", "({1} in {0})", ["str", "str"], "bool"),
    ("eq", "({0} == {1})", ["str", "str"], "bool"),
    ("lt", "({0} < {1})", ["str", "str"], "bool"),
    ("le", "({0} <= {1})", ["str", "str"], "bool"),
    ("gt", "({0} > {1})", ["str", "str"], "bool"),
    ("isdigit", "{0}.isdigit()", ["str"], "bool"),
    ("isalpha", "{0}.isalpha()", ["str"], "bool"),
    ("isalnum", "{0}.isalnum()", ["str"], "bool"),
    ("isspace", "{0}.isspace()", ["str"], "bool"),
    ("isupper", "{0}.isupper()", ["str"], "bool"),
    ("islower", "{0}.islower()", ["str"], "bool"),
    ("istitle", "{0}.istitle()", ["str"], "bool"),
    ("isidentifier", "{0}.isidentifier()", ["str"], "bool"),
    ("truth", "bool({0})", ["str"], "bool"),
]

JAVA_OPS = [
    # strings
    ("concat", "{0}.concat({1})", ["str", "str"], "str"),
    ("plus", "({0} + {1})", ["str", "str"], "str"),
    ("plus-int", "({0} + {1})", ["str", "int"], "str"),
    ("substring", "{0}.substring({1})", ["str", "int"], "str"),
    ("substring2", "{0}.substring({1}, {2})", ["str", "int", "int"], "str"),
    ("replace-char", "{0}.replace({1}, {2})", ["str", "jchar", "jchar"],
     "str"),
    ("replace", "{0}.replace({1}, {2})", ["str", "str", "str"], "str"),
    ("upper", "{0}.toUpperCase()", ["str"], "str"),
    ("lower", "{0}.toLowerCase()", ["str"], "str"),
    ("trim", "{0}.trim()", ["str"], "str"),
    ("value-of-int", "String.valueOf({0})", ["int"], "str"),
    ("value-of-char", "String.valueOf({0})", ["jchar"], "str"),
    ("value-of-bool", "String.valueOf({0})", ["jbool"], "str"),
    ("int-to-string", "Integer.toString({0})", ["int"], "str"),
    ("int-to-hex", "Integer.toHexString({0})", ["int"], "str"),
    ("sb-append", "new StringBuilder({0}).append({1}).toString()",
     ["str", "str"], "str"),
    ("sb-append-int", "new StringBuilder({0}).append({1}).toString()",
     ["str", "int"], "str"),
    ("sb-insert", "new StringBuilder({0}).insert({1}, {2}).toString()",
     ["str", "int", "str"], "str"),
    ("sb-delete", "new StringBuilder({0}).delete({1}, {2}).toString()",
     ["str", "int", "int"], "str"),
    ("sb-delete-char", "new StringBuilder({0}).deleteCharAt({1}).toString()",
     ["str", "int"], "str"),
    ("sb-reverse", "new StringBuilder({0}).reverse().toString()", ["str"],
     "str"),
    ("sb-set-length",
     "sbSetLength({0}, {1})", ["str", "small"], "str"),
    ("sb-set-char", "sbSetCharAt({0}, {1}, {2})", ["str", "int", "jchar"],
     "str"),
    ("format-s", "String.format(\"%s-%s\", {0}, {1})", ["str", "str"],
     "str"),
    ("format-d", "String.format(\"%d\", {0})", ["int"], "str"),
    # integers
    ("length", "{0}.length()", ["str"], "int"),
    ("char-at", "(int) {0}.charAt({1})", ["str", "int"], "int"),
    ("code-point-at", "{0}.codePointAt({1})", ["str", "int"], "int"),
    ("index-of", "{0}.indexOf({1})", ["str", "str"], "int"),
    ("index-of-from", "{0}.indexOf({1}, {2})", ["str", "str", "int"], "int"),
    ("index-of-char", "{0}.indexOf({1})", ["str", "jchar"], "int"),
    ("last-index-of", "{0}.lastIndexOf({1})", ["str", "str"], "int"),
    ("last-index-of-from", "{0}.lastIndexOf({1}, {2})",
     ["str", "str", "int"], "int"),
    ("last-index-of-char", "{0}.lastIndexOf({1})", ["str", "jchar"], "int"),
    ("compare-to", "Integer.signum({0}.compareTo({1}))", ["str", "str"],
     "int"),
    ("compare-ic", "Integer.signum({0}.compareToIgnoreCase({1}))",
     ["str", "str"], "int"),
    ("parse-int", "Integer.parseInt({0})", ["str"], "int"),
    ("hash-code", "{0}.hashCode()", ["str"], "int"),
    # Booleans
    ("equals", "{0}.equals({1})", ["str", "str"], "bool"),
    ("equals-ic", "{0}.equalsIgnoreCase({1})", ["str", "str"], "bool"),
    ("contains", "{0}.contains({1})", ["str", "str"], "bool"),
    ("starts-with", "{0}.startsWith({1})", ["str", "str"], "bool"),
    ("starts-with-off", "{0}.startsWith({1}, {2})", ["str", "str", "int"],
     "bool"),
    ("ends-with", "{0}.endsWith({1})", ["str", "str"], "bool"),
    ("is-empty", "{0}.isEmpty()", ["str"], "bool"),
]

PY_EXCEPTIONS = {"ValueError", "IndexError", "TypeError", "OverflowError"}
JAVA_EXCEPTIONS = {
    "StringIndexOutOfBoundsException", "IndexOutOfBoundsException",
    "NumberFormatException", "NullPointerException",
    "IllegalArgumentException",
}


class Generator:
    """Random typed expressions over pinned inputs."""

    def __init__(self, rng, lang, depth):
        self.rng = rng
        self.lang = lang
        self.depth = depth
        self.ops = PY_OPS if lang == "python" else JAVA_OPS
        self.inputs = {}  # name -> (type, value)

    def leaf(self, typ):
        rng = self.rng
        if typ == "str":
            if rng.random() < 0.8:
                name = rng.choice(["s", "t", "u"])
                if name not in self.inputs:
                    self.inputs[name] = ("str", random_string(rng))
                return name
            return self.literal("str", random_string(rng, 2))
        if typ == "int":
            if rng.random() < 0.7:
                name = rng.choice(["k", "j"])
                if name not in self.inputs:
                    self.inputs[name] = ("int", rng.randint(-6, 8))
                return name
            return self.literal("int", rng.randint(-3, 5))
        if typ == "small":
            return self.literal("int", rng.randint(0, 6))
        if typ == "part":
            return self.literal("int", rng.randint(0, 2))
        if typ == "code":
            return self.literal(
                "int", rng.choice([0, 9, 32, 65, 97, 122, 233, 0x263A, -1,
                                   0x110000]))
        if typ == "jchar":
            return self.literal("jchar", rng.choice("abA .1é"))
        if typ == "jbool":
            return self.literal("jbool", rng.random() < 0.5)
        raise ValueError(typ)

    def literal(self, typ, value):
        if self.lang == "python":
            return repr(value)
        if typ == "str":
            return java_string_literal(value)
        if typ == "int":
            return str(value) if value >= 0 else "(" + str(value) + ")"
        if typ == "jchar":
            return java_char_literal(value)
        if typ == "jbool":
            return "true" if value else "false"
        raise ValueError(typ)

    def expr(self, typ, depth):
        candidates = [op for op in self.ops if op[3] == typ]
        if depth <= 0 or not candidates or (
                depth < self.depth and self.rng.random() < 0.5):
            return self.leaf(typ), []
        name, template, args, _ = self.rng.choice(candidates)
        rendered, names = [], [name]
        for a in args:
            # only nest string arguments, to keep values small
            if a == "str" and depth > 1 and self.rng.random() < 0.4:
                e, n = self.expr("str", depth - 1)
                names += n
            else:
                e = self.leaf(a)
            rendered.append(e)
        return template.format(*rendered), names

    def case(self):
        typ = self.rng.choice(["str", "int", "bool"])
        candidates = [op for op in self.ops if op[3] == typ]
        name, template, args, _ = self.rng.choice(candidates)
        rendered, names = [], [name]
        for a in args:
            if a == "str" and self.depth > 1 and self.rng.random() < 0.3:
                e, n = self.expr("str", self.depth - 1)
                names += n
            else:
                e = self.leaf(a)
            rendered.append(e)
        return template.format(*rendered), typ, names


def java_string_literal(s):
    out = '"'
    for ch in s:
        if ch in '"\\':
            out += "\\" + ch
        elif ch == "\t":
            out += "\\t"
        elif ord(ch) < 0x20 or ord(ch) > 0x7e:
            out += "\\u%04x" % ord(ch)
        else:
            out += ch
    return out + '"'


def java_char_literal(c):
    if c == "'" or c == "\\":
        return "'\\" + c + "'"
    if ord(c) < 0x20 or ord(c) > 0x7e:
        return "'\\u%04x'" % ord(c)
    return "'" + c + "'"


# ---------------------------------------------------------------------------
# Python
# ---------------------------------------------------------------------------

def python_expected(case):
    env = {n: v for n, (_, v) in case["inputs"].items()}
    try:
        value = eval(case["expr"], {}, env)  # generated, trusted
    except Exception as e:  # noqa: BLE001 - we record any exception
        return ("raises", type(e).__name__)
    if isinstance(value, bool):
        return ("value", value)
    if isinstance(value, (int, str)):
        if isinstance(value, str) and len(value) > 40:
            return None
        return ("value", value)
    return None


def python_program(case):
    lines = []
    conds = []
    for name, (typ, value) in sorted(case["inputs"].items()):
        lines.append(f"{name} = nondet_{'str' if typ == 'str' else 'int'}()")
        conds.append(f"{name} == {value!r}")
    lines.append("if " + (" and ".join(conds) or "True") + ":")
    kind, value = case["expected"]
    if kind == "value":
        lines.append(f"    r = {case['expr']}")
        true_line = len(lines) + 1
        lines.append(f"    assert r == {value!r}")
        false_line = len(lines) + 1
        lines.append(f"    assert r != {value!r}")
    else:
        lines.append("    try:")
        lines.append(f"        r = {case['expr']}")
        true_line = len(lines) + 1
        lines.append("        assert False")
        lines.append(f"    except {value}:")
        false_line = len(lines) + 1
        lines.append("        assert False")
    return "\n".join(lines) + "\n", true_line, false_line


# --python-raising-ops-check makes operations whose success the front end
# cannot prove may-raise; without it they never raise, by design.
PY_COMMON = ["--python-raising-ops-check"]
PY_BACKENDS = {
    "refined": PY_COMMON,
    "native-cvc5": PY_COMMON + ["--cvc5", "--python-smt-strings"],
    "native-z3": PY_COMMON + ["--z3", "--python-smt-strings"],
    "refined-z3": PY_COMMON + ["--z3"],
}

# ---------------------------------------------------------------------------
# Java
# ---------------------------------------------------------------------------

JAVA_HELPERS = textwrap.dedent("""\
    static String sbSetLength(String s, int n) {
      StringBuilder b = new StringBuilder(s);
      b.setLength(n);
      return b.toString();
    }
    static String sbSetCharAt(String s, int i, char c) {
      StringBuilder b = new StringBuilder(s);
      b.setCharAt(i, c);
      return b.toString();
    }
""")

JAVA_TYPES = {"str": "String", "int": "int", "bool": "boolean"}


def java_params(case):
    return ", ".join(
        ("String " if typ == "str" else "int ") + name
        for name, (typ, _) in sorted(case["inputs"].items()))


def java_oracle_class(cases):
    """A class printing the value of each case, one JSON line each."""
    out = ["public class Oracle {", textwrap.indent(JAVA_HELPERS, "  ")]
    for i, c in enumerate(cases):
        typ = JAVA_TYPES[c["type"]]
        out.append(f"  static {typ} e{i}({java_params(c)}) {{")
        out.append(f"    return {c['expr']};")
        out.append("  }")
    out.append("  static String q(String s) {")
    out.append("    StringBuilder b = new StringBuilder(\"\\\"\");")
    out.append("    for (char ch : s.toCharArray()) {")
    out.append("      if (ch == '\"' || ch == '\\\\') b.append('\\\\');")
    out.append("      if (ch < 0x20 || ch > 0x7e)"
               " b.append(String.format(\"\\\\u%04x\", (int) ch));")
    out.append("      else b.append(ch);")
    out.append("    }")
    out.append("    return b.append('\"').toString();")
    out.append("  }")
    out.append("  public static void main(String[] a) {")
    for i, c in enumerate(cases):
        args = ", ".join(
            java_string_literal(v) if typ == "str" else str(v)
            for _, (typ, v) in sorted(c["inputs"].items()))
        if c["type"] == "str":
            show = f"\"\\\"value\\\": \" + q(e{i}({args}))"
        else:
            show = f"\"\\\"value\\\": \" + e{i}({args})"
        out.append("    try {")
        out.append(f"      System.out.println(\"{{\\\"case\\\": {i}, \" + "
                   f"{show} + \"}}\");")
        out.append("    } catch (Exception x) {")
        out.append(f"      System.out.println(\"{{\\\"case\\\": {i}, "
                   f"\\\"raises\\\": \\\"\" + x.getClass().getSimpleName()"
                   f" + \"\\\"}}\");")
        out.append("    }")
    out.append("  }")
    out.append("}")
    return "\n".join(out) + "\n"


def java_value_literal(typ, value):
    if typ == "str":
        return java_string_literal(value)
    if typ == "bool":
        return "true" if value else "false"
    return str(value) if value >= 0 else "(" + str(value) + ")"


def java_case_method(i, case, lines):
    """Append the check method of case i to lines; return the lines of its
    true and false checks."""
    conds = []
    for name, (typ, value) in sorted(case["inputs"].items()):
        if typ == "str":
            conds.append(f"!{name}.equals({java_string_literal(value)})")
        else:
            conds.append(f"{name} != {java_value_literal('int', value)}")
    lines.append(f"  public static void c{i}({java_params(case)}) {{")
    if conds:
        lines.append(f"    if({' || '.join(conds)}) return;")
    kind, value = case["expected"]
    typ = JAVA_TYPES[case["type"]]
    if kind == "value":
        lines.append(f"    {typ} r = {case['expr']};")
        lit = java_value_literal(case["type"], value)
        eq = f"r.equals({lit})" if case["type"] == "str" else f"r == {lit}"
        true_line = len(lines) + 1
        lines.append(f"    assert {eq};")
        false_line = len(lines) + 1
        lines.append(f"    assert !({eq});")
    else:
        lines.append("    try {")
        lines.append(f"      {typ} r = {case['expr']};")
        true_line = len(lines) + 1
        lines.append("      assert false;")
        lines.append(f"    }} catch({value} x) {{")
        false_line = len(lines) + 1
        lines.append("      assert false;")
        lines.append("    }")
    lines.append("  }")
    return true_line, false_line


JAVA_BACKENDS = {
    "refined": [],
}


# ---------------------------------------------------------------------------
# Running
# ---------------------------------------------------------------------------

def bounded(cmd, timeout, mem_mb, cwd):
    """Run cmd with an address-space cap of mem_mb and a time limit."""
    shell = (f"ulimit -v {mem_mb * 1024}; exec timeout {timeout} "
             + " ".join(shlex_quote(c) for c in cmd))
    try:
        p = subprocess.run(["bash", "-c", shell], cwd=cwd,
                           capture_output=True, text=True,
                           timeout=timeout + 30, errors="replace")
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return 124, "timeout (outer)"


def shlex_quote(s):
    import shlex
    return shlex.quote(s)


PROPERTY_RE = re.compile(
    r"^\[(?P<name>[^\]]+)\] (?:file \S+ )?line (?P<line>\d+) .*: "
    r"(?P<status>SUCCESS|FAILURE|ERROR|UNKNOWN)$")


def classify(rc, output, true_line, false_line, function=None):
    if rc == 124:
        return "timeout", {}
    status = {}
    for line in output.splitlines():
        m = PROPERTY_RE.match(line.strip())
        if not m:
            continue
        name = m.group("name")
        # only the assertions of the case (not null-pointer or exception
        # checks on the same line)
        if "assertion" not in name:
            continue
        if function is not None and f".{function}:" not in name:
            continue
        status.setdefault(int(m.group("line")), set()).add(m.group("status"))
    t = status.get(true_line, set())
    f = status.get(false_line, set())
    details = {"true": sorted(t), "false": sorted(f)}
    if "SUCCESS" in f and "FAILURE" not in f:
        return "unsound", details
    if "VERIFICATION ERROR" in output or "ERROR" in t | f or not t or not f:
        return "error", details
    if t == {"SUCCESS"} and f == {"FAILURE"}:
        return "ok", details
    return "imprecise", details


def run_python_case(args, case, backend, flags):
    work = Path(args.out) / "work" / f"{case['id']}-{backend}"
    work.mkdir(parents=True, exist_ok=True)
    program, tl, fl = python_program(case)
    (work / "main.py").write_text(program)
    cmd = [args.cbmc, "main.py"] + flags
    rc, output = bounded(cmd, args.timeout, args.mem_mb, work)
    verdict, details = classify(rc, output, tl, fl)
    return work, cmd, output, verdict, details


def run_java_case(args, case, backend, flags, classdir):
    work = Path(args.out) / "work" / f"{case['id']}-{backend}"
    work.mkdir(parents=True, exist_ok=True)
    i = case["index"]
    cp = ":".join([str(classdir), str(REPO / "jbmc/lib/java-models-library"
                                       "/target/core-models.jar"),
                   str(REPO / "jbmc/lib/java-models-library/target"
                       "/cprover-api.jar")])
    cmd = [args.jbmc, "Check", "--function", f"Check.c{i}", "--cp", cp,
           "--java-assume-inputs-non-null", "--max-nondet-string-length",
           "8"] + flags
    rc, output = bounded(cmd, args.timeout, args.mem_mb, work)
    verdict, details = classify(rc, output, case["true_line"],
                                case["false_line"], function=f"c{i}")
    return work, cmd, output, verdict, details


def generate(args, rng):
    cases = []
    seen = set()
    attempts = 0
    while len(cases) < args.count and attempts < args.count * 50:
        attempts += 1
        g = Generator(rng, args.lang, args.depth)
        expr, typ, names = g.case()
        if expr in seen:
            continue
        seen.add(expr)
        cases.append({"id": f"c{len(cases):05d}", "index": len(cases),
                      "expr": expr, "type": typ, "ops": names,
                      "inputs": {n: g.inputs[n] for n in g.inputs
                                 if re.search(r"\b" + n + r"\b", expr)}})
    return cases


def java_oracle(args, cases):
    odir = Path(args.out) / "oracle"
    odir.mkdir(parents=True, exist_ok=True)
    (odir / "Oracle.java").write_text(java_oracle_class(cases))
    subprocess.run(["javac", "-encoding", "UTF-8", "-d", str(odir),
                    str(odir / "Oracle.java")], check=True)
    p = subprocess.run(["java", "-cp", str(odir), "Oracle"],
                       capture_output=True, text=True, check=True)
    results = {}
    for line in p.stdout.splitlines():
        r = json.loads(line)
        if "raises" in r:
            results[r["case"]] = ("raises", r["raises"])
        else:
            results[r["case"]] = ("value", r["value"])
    return results


def java_check_class(args, cases):
    cdir = Path(args.out) / "classes"
    cdir.mkdir(parents=True, exist_ok=True)
    lines = ["public class Check {"]
    lines += textwrap.indent(JAVA_HELPERS, "  ").splitlines()
    for c in cases:
        c["true_line"], c["false_line"] = java_case_method(
            c["index"], c, lines)
    lines.append("}")
    (cdir / "Check.java").write_text("\n".join(lines) + "\n")
    subprocess.run(["javac", "--release", "8", "-g", "-encoding", "UTF-8",
                    "-d", str(cdir), str(cdir / "Check.java")], check=True,
                   capture_output=True)
    return cdir


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--lang", choices=["python", "java"], default="python")
    ap.add_argument("--backends", default=None,
                    help="comma-separated; python: " + ",".join(PY_BACKENDS)
                    + "; java: " + ",".join(JAVA_BACKENDS))
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--depth", type=int, default=2,
                    help="1: one function; 2: up to one nested function")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--mem-mb", type=int, default=6000)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cbmc", default=str(REPO / "build/bin/cbmc"))
    ap.add_argument("--jbmc", default=str(REPO / "build/bin/jbmc"))
    ap.add_argument("--replay", default=None,
                    help="results.jsonl of an earlier run: check its cases "
                    "again instead of generating new ones")
    ap.add_argument("--replay-verdicts", default="unsound,error",
                    help="with --replay, the verdicts (on any back end) of "
                    "the cases to check again")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    out = Path(args.out)
    if (out / "work").exists():
        shutil.rmtree(out / "work")
    out.mkdir(parents=True, exist_ok=True)

    table = PY_BACKENDS if args.lang == "python" else JAVA_BACKENDS
    backends = (args.backends.split(",") if args.backends
                else list(table))
    if args.replay:
        wanted = set(args.replay_verdicts.split(","))
        old = [json.loads(line) for line in open(args.replay)]
        ids = {r["id"] for r in old if r["verdict"] in wanted}
        cases, seen = [], set()
        for r in old:
            if r["id"] in ids and r["id"] not in seen:
                seen.add(r["id"])
                cases.append({"id": r["id"], "index": len(cases),
                              "expr": r["expr"], "type": r.get("type"),
                              "ops": r["ops"], "inputs": {
                                  n: tuple(v) for n, v in
                                  r["inputs"].items()}})
    else:
        cases = generate(args, rng)

    if args.lang == "python":
        kept = []
        for c in cases:
            exp = python_expected(c)
            if exp is None or (exp[0] == "raises"
                               and exp[1] not in PY_EXCEPTIONS):
                continue
            c["expected"] = exp
            kept.append(c)
        cases = kept
    else:
        oracle = java_oracle(args, cases)
        kept = []
        for c in cases:
            exp = oracle.get(c["index"])
            if exp is None or (exp[0] == "raises"
                               and exp[1] not in JAVA_EXCEPTIONS):
                continue
            if exp[0] == "value" and c["type"] == "bool":
                exp = ("value", exp[1] == "true" or exp[1] is True)
            if exp[0] == "value" and c["type"] == "str":
                exp = ("value", json.loads(json.dumps(exp[1])))
            c["expected"] = exp
            kept.append(c)
        cases = kept
        classdir = java_check_class(args, cases)

    jobs = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for c in cases:
            for b in backends:
                if args.lang == "python":
                    f = pool.submit(run_python_case, args, c, b, table[b])
                else:
                    f = pool.submit(run_java_case, args, c, b, table[b],
                                    classdir)
                jobs.append((c, b, f))
        results = []
        for c, b, f in jobs:
            work, cmd, output, verdict, details = f.result()
            results.append({"id": c["id"], "backend": b, "expr": c["expr"],
                            "type": c["type"],
                            "inputs": c["inputs"], "ops": c["ops"],
                            "expected": c["expected"], "verdict": verdict,
                            "details": details})
            if verdict == "ok":
                shutil.rmtree(work)
            else:
                (work / "output.txt").write_text(
                    " ".join(cmd) + "\n\n" + output)

    with open(out / "results.jsonl", "w") as fh:
        for r in results:
            fh.write(json.dumps(r, ensure_ascii=False) + "\n")

    summary = collections.Counter((r["backend"], r["verdict"])
                                  for r in results)
    lines = [f"{len(cases)} cases, back ends: {', '.join(backends)}", ""]
    for b in backends:
        counts = {v: summary[(b, v)] for v in
                  ["ok", "unsound", "imprecise", "error", "timeout"]}
        lines.append(f"{b:12} " + "  ".join(f"{v} {n}"
                                            for v, n in counts.items()))
    for verdict in ["unsound", "error", "imprecise", "timeout"]:
        bad = [r for r in results if r["verdict"] == verdict]
        if not bad:
            continue
        lines += ["", f"== {verdict} (by outermost operation)"]
        by_op = collections.defaultdict(list)
        for r in bad:
            by_op[r["ops"][0]].append(r)
        for op, rs in sorted(by_op.items(), key=lambda x: -len(x[1])):
            lines.append(f"{op}: {len(rs)}")
            for r in rs[:4]:
                lines.append(f"    {r['id']} {r['backend']}: {r['expr']}  "
                             f"inputs={r['inputs']} "
                             f"expected={r['expected']}")
    (out / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    sys.exit(main())
