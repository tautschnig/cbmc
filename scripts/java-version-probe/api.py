#!/usr/bin/env python3
"""List the public API of the JBMC models that the probe calls.

Reads the output of `javap -public` on the classes of core-models.jar and
writes one line per constructor or method:

    CLASS|ctor|<init>|PARAM,PARAM,...|
    CLASS|static|NAME|PARAM,...|RETURN
    CLASS|instance|NAME|PARAM,...|RETURN

with the erasure of each type (generics removed, varargs as arrays).

Usage: api.py JAVAP_OUTPUT > api.txt
"""

import re
import sys

CLASS_RE = re.compile(r"^(?:public )?(?:abstract |final )*(?:class|interface) "
                      r"([\w.$]+)")
MEMBER_RE = re.compile(
    r"^  public (?P<mods>(?:(?:static|final|native|synchronized|abstract|"
    r"default|strictfp) )*)(?:<[^(]*> )?(?P<ret>[\w.$<>?,\[\] ]+? )?"
    r"(?P<name>[\w.$]+)\((?P<params>[^)]*)\)")


def erase(t):
    t = t.strip()
    depth = 0
    out = []
    for ch in t:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out.append(ch)
    t = "".join(out).strip()
    if t.endswith("..."):
        t = t[:-3] + "[]"
    # a type variable (T, R) erases to Object
    if re.fullmatch(r"[A-Z]", t.rstrip("[]")):
        t = "java.lang.Object" + t[len(t.rstrip("[]")):]
    return t


def split_params(params):
    out, depth, cur = [], 0, []
    for ch in params:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        out.append("".join(cur))
    return [erase(p) for p in out]


def main():
    cls = None
    for line in open(sys.argv[1]):
        m = CLASS_RE.match(line)
        if m:
            cls = m.group(1)
            continue
        m = MEMBER_RE.match(line)
        if not m or cls is None:
            continue
        params = ",".join(split_params(m.group("params")))
        name = m.group("name")
        if name == cls:
            print(f"{cls}|ctor|<init>|{params}|")
        else:
            kind = "static" if "static" in m.group("mods") else "instance"
            ret = erase(m.group("ret") or "void")
            print(f"{cls}|{kind}|{name}|{params}|{ret}")


if __name__ == "__main__":
    main()
