# Per-instance soundness AND precision: an int wrapped into python_value is
# boxed behind a FRESH per-execution heap object, so a dict built by a
# construction site reached more than once (here mk, called twice) does not
# alias its boxed int across instances. d1["v"] is really 100 (not d2's 200).
# Before the per-instance fix this aliased (a false proof: d1["v"] == 200 was
# wrongly provable, via the dict-literal const-fold re-reading the boxed
# pointer).


def mk(n: int) -> dict:
    return {"v": n, "s": "x"}


d1 = mk(100)
d2 = mk(200)
assert d1["v"] == 100
assert d2["v"] == 200
