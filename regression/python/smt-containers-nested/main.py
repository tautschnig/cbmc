# P3a of doc/python-frontend-unbounded-containers-plan.md
# (--python-smt-containers): NESTED containers. Under the flag a
# container-typed element/value slot holds a BOXED python_value (the
# existing __class_ptr/__list_ptr handle discipline) instead of an
# inline struct — an inline container would nest infinite arrays
# (no byte width) and force whole-struct copies, which is exactly the
# COPY semantics PLR §3.1 forbids for nested mutables. Boxing gives
# the required REFERENCE semantics: a dict read from a list IS the
# object.
from typing import Any, List, Dict

# annotated nested types agree with literal displays
xs = [{"a": 1}, {"a": 2}]
assert xs[0]["a"] == 1
assert xs[1]["a"] == 2

# PLR §3.1 reference semantics
d = xs[0]
d["a"] = 99
assert xs[0]["a"] == 99


def count_named(items: List[Dict[str, Any]]) -> int:
    n = 0
    for item in items:
        if item.get('name') == 'a':
            n += 1
    return n


assert count_named([{'name': 'a'}, {'name': 'b'}]) == 1


def group(items: List[Dict[str, Any]]) -> None:
    seen: dict = {}
    for item in items:
        key = item.get('name', 'Unknown')
        if key not in seen:
            seen[key] = 1


group([{'name': 'a'}, {'name': 'b'}])


# mutation THROUGH an untyped parameter (boxed lvalue, PLR §3.1)
def rm(p) -> None:
    del p["x"]


d2 = {"x": 1, "y": 2}
rm(d2)
assert len(d2) == 1
assert not ("x" in d2)

# (Subscript-WRITE of a NEW key through an untyped parameter with a
# value readback is a PRE-EXISTING residual on the bounded model too —
# the dict-by-reference per-instance-identity plan's territory, see
# doc/python-frontend-dict-byref-plan.md — so it is not pinned here.)
