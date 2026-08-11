# PLR §3.1: lists are mutable objects; passing a list to a function
# binds the parameter to the same object, so mutations through the
# parameter are visible at the call site (Python's reference
# semantics for mutable containers, in contrast to C-style by-value
# struct copies).
#
# Stage 1 of the by-reference refactor: list/dict parameters are
# emitted as pointer-to-struct in the goto, the call site wraps
# arguments with address_of (materialising literal struct
# arguments into a temp first), and convert_name auto-dereferences
# pointer-typed parameter symbols at use sites.

def append_to(xs: list[int], v: int) -> None:
    xs.append(v)

ys: list[int] = [1, 2, 3]
append_to(ys, 99)
assert len(ys) == 4
assert ys[3] == 99


def add_kv(d: dict[str, int], k: str, v: int) -> None:
    d[k] = v

mp: dict[str, int] = {"a": 1}
add_kv(mp, "b", 2)
assert len(mp) == 2
assert mp["b"] == 2


# Literal-argument case (the call-site materialisation path):
def length(xs: list[int]) -> int:
    return len(xs)

assert length([10, 20, 30]) == 3
