# Soundness: an unbounded int wrapped into python_value is over-approximated to
# nondet (CBMC cannot store distinct per-instance integer_typet heap objects, so
# a precise box would alias across instances of the same construction site). The
# over-approximation must NOT let a wrong value be proven. mk is one construction
# site reached twice; d1["v"] is really 100, so the (false) claim d1["v"] == 200
# must NOT verify. Before the over-approximation the boxed int aliased d2's value
# and this was a FALSE PROOF (wrongly SUCCESSFUL).


def mk(n: int) -> dict:
    return {"v": n, "s": "x"}


d1 = mk(100)
d2 = mk(200)
assert d1["v"] == 200
