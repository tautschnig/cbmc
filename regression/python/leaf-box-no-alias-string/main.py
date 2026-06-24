# Leaf-boxing must use a FRESH per-execution heap object, not a shared static
# symbol: a string-keyed / string-valued dict built by a construction site that
# runs more than once (a function returning it, a loop) must not alias its boxed
# leaves across instances. Regression for the static-symbol aliasing that the
# per-instance allocation (allocate_boxed_leaf) fixes.


def mk_keyed(k: str, v: int) -> dict:
    return {k: v}


d1 = mk_keyed("a", 1)
d2 = mk_keyed("b", 2)
# Without per-instance boxing d1's key aliased d2's "b".
assert d1["a"] == 1
assert "a" in d1
assert "b" not in d1
assert d2["b"] == 2


def mk_valued(s: str) -> dict:
    # heterogeneous -> the string value is wrapped in python_value (__str box)
    return {"v": s, "n": 0}


e1 = mk_valued("x")
e2 = mk_valued("y")
assert e1["v"] == "x"
assert e2["v"] == "y"
