# Closed-form min/max (P3): witness m with exists-witness AND
# forall-bound; INT elements only (a float NaN element makes the
# forall-bound false while CPython's min/max ignore or propagate NaN
# by position -- floats keep the bounded reduction). Empty-sequence
# ValueError is catchable (PLR 8.4).
def fetch() -> list[int]: ...


xs = fetch()
if len(xs) > 2 and xs[0] == 3 and xs[1] == 9 and len(xs) == 3 and xs[2] == 5:
    assert max(xs) == 9
    assert min(xs) == 3
if len(xs) > 0:
    m = max(xs)
    n = min(xs)
    assert n <= m
    assert m in xs

assert max([4, 1, 7]) == 7
assert min([4, 1, 7]) == 1

hit = 0
try:
    max([])
except ValueError:
    hit = 1
assert hit == 1
