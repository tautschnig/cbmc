# KNOWNBUG: residual collections gaps (both loud). NARROWED
# 2026-08-12: the plain str-VALUE dict read-back (d[k] = "x";
# d[k] == "x", incl. Dict[str, str] and defaultdict stores) is
# FIXED (dict_literals accepts string-literal values); what remains:
# 1. the str-FACTORY MISS read (len(d['missing']) == 0 on an empty
#    defaultdict(str)) -- the found-ternary blocks the fold; needs
#    the read to fold to the factory zero when the tracked literal
#    proves the key absent.
# 2. string-keyed Counter accumulation -- the library Counter model
#    is tuple-keyed composition; string keys are over-approximated.
from collections import defaultdict, Counter


def str_factory() -> None:
    d = defaultdict(str)
    # Factory-zero miss read: "" (falsy, len 0).
    v = d["missing"]
    assert len(v) == 0
    # Store-then-read-back.
    d["name"] = "hello"
    assert d["name"] == "hello"


def counter() -> None:
    c = Counter()
    c["x"] += 1
    c["x"] += 1
    c["x"] += 1
    assert c["x"] == 3
    assert c["nope"] == 0


str_factory()
counter()
