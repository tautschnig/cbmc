def fetch() -> list[int]: ...


xs = fetch()
ys = [i for i, v in enumerate(xs)]
assert len(ys) == len(xs)
zs = [v * 2 for i, v in enumerate(xs)]
assert len(zs) == len(xs)
if len(xs) > 3:
    assert ys[2] == 2
    assert zs[2] == xs[2] * 2
ws = [i + v for i, v in enumerate(xs, 10)]
if len(xs) > 1:
    assert ws[1] == 11 + xs[1]
# concrete (CPython-validated below)
cs = [5, 7, 9]
ds = [i * 100 + v for i, v in enumerate(cs)]
assert ds[0] == 5 and ds[1] == 107 and ds[2] == 209
def fetch() -> list[int]: ...


def fetch2() -> list[int]: ...


a = fetch()
b = fetch2()
s = [x + y for x, y in zip(a, b)]
# PLR: zip stops at the shortest
if len(a) < len(b):
    assert len(s) == len(a)
if len(b) < len(a):
    assert len(s) == len(b)
if len(a) > 2 and len(b) > 2:
    assert s[1] == a[1] + b[1]
# concrete
u = [x * y for x, y in zip([2, 3], [10, 20, 30])]
assert len(u) == 2 and u[0] == 20 and u[1] == 60
