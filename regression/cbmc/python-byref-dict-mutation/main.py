# PLR §3.1: dicts are mutable. Stage 2 of the by-reference refactor:
# the mutation-emitting helpers (clear, pop, popitem, dict-subscript-
# assign, etc.) had `obj.id() == ID_symbol` guards that excluded the
# dereference-of-pointer-parameter shape introduced by Stage 1.
# Stage 2 relaxes those guards to also accept dereference_exprt, and
# guards the to_symbol_expr-based literal-tracking lookups so they
# don't crash on non-symbol receivers.

def reset(d: dict[str, int]) -> None:
    d.clear()

m: dict[str, int] = {"a": 1, "b": 2}
reset(m)
assert len(m) == 0


def add(d: dict[str, int]) -> None:
    d["c"] = 99

m2: dict[str, int] = {"a": 1, "b": 2}
add(m2)
assert m2["c"] == 99
assert len(m2) == 3


def pop_a(d: dict[str, int]) -> int:
    return d.pop("a", 0)

m3: dict[str, int] = {"a": 1, "b": 2}
v = pop_a(m3)
assert v == 1
assert len(m3) == 1
