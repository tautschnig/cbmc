def fetch() -> list[int]: ...


xs = fetch()
ys = [x * 1000000000000 for x in xs]     # products beyond 64-bit
assert len(ys) == len(xs)
if len(xs) > 2 and xs[1] == 10 ** 15:
    assert ys[1] == 10 ** 27
b = all(x * x >= 0 for x in xs)
assert b
big = 2 ** 100
assert big > 2 ** 99
def fetch() -> list[int]: ...


xs = fetch()
# quantified membership + witness index under math ints
if len(xs) > 3 and xs[2] == 2 ** 70:
    assert (2 ** 70) in xs
    assert xs.index(2 ** 70) <= 2
