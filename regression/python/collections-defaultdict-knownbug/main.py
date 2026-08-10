# KNOWNBUG: two residual collections gaps (both loud, split out of
# regression/cbmc/python-defaultdict-counter):
# 1. str-VALUE dict read-back (d[k] = "x"; d[k] == "x") through the
#    boxed scan under refined strings -- predates the defaultdict
#    intrinsic (fails on plain Dict[str, str] too).
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
