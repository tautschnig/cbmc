def fetch() -> list[int]: ...


xs = fetch()
ys = [x for x in xs]     # closed-form identity map
assert xs == ys          # quantified equality at symbolic length
zs = [x + 1 for x in xs]
if len(xs) > 0:
    assert xs != zs
# concrete twins
a = [1, 2, 3]
b = [1, 2, 3]
c = [1, 2, 4]
assert a == b
assert a != c
assert [] == []
assert not ([] == [1])
