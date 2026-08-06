def fetch() -> list[int]: ...


xs = fetch()
# ground truth wiring at symbolic length
if len(xs) > 3 and xs[2] == 7:
    assert 7 in xs
    assert not (7 not in xs)
b = 5 in xs
assert b or not b
# concrete twins (CPython-checked)
ys = [1, 2, 3]
assert 2 in ys
assert 9 not in ys
assert not (None in ys)
zs = []
assert 1 not in zs
