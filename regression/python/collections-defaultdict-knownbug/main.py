# Formerly KNOWNBUG; all acceptance criteria hold (2026-08-13):
# str-value read-back folds; the str-factory MISS read folds to the
# factory zero via the tracked-literal absent-key arm; string-keyed
# Counter accumulation works via key-type dispatch in the library
# model + the aug-assign dunder desugaring.
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
