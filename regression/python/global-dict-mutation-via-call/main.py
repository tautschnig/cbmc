# PLR 7.12 + object identity: a function mutating a module-global dict
# must be reflected at a later module read. Previously the subscript-
# assign was dropped (global not dict-typed in the function body) and
# membership/lookup folded a stale dict_literals -> false proofs.
d = {"a": 1}


def add_b() -> None:
    d["b"] = 2


def set_a(v: int) -> None:
    d["a"] = v


def unrelated() -> None:
    pass


add_b()
assert len(d) == 2
assert d["b"] == 2
set_a(99)
assert d["a"] == 99
unrelated()
assert d["a"] == 99  # unrelated() did not change it
