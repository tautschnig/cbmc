# Closed-form list.index (P3): witness k with first-occurrence
# minimality (exists match AND forall j < k: no match), ValueError
# when absent (catchable, PLR 8.4). Exact at symbolic length.
def fetch() -> list[int]: ...


xs = fetch()
if len(xs) > 4 and xs[0] == 9 and xs[1] == 7 and xs[3] == 7:
    assert xs.index(7) == 1     # FIRST occurrence, not 3
    assert xs.index(9) == 0

# concrete twins (CPython-validated)
ys = [5, 3, 5, 1]
assert ys.index(5) == 0
assert ys.index(3) == 1
assert ys.index(1) == 3

# PLR: ValueError when absent -- catchable
hit = 0
try:
    ys.index(9)
except ValueError:
    hit = 1
assert hit == 1
