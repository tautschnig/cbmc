# Soundness: a side-effecting call embedded in a membership container must be
# evaluated once. f(h) returns 1, so `2 in [f(h)]` is False; a double-eval
# (f re-pops -> 2) would false-prove it.
def f(l: list) -> int:
    x = l[0]
    del l[0]
    return x


h = [1, 2, 3]
assert 2 in [f(h)]
