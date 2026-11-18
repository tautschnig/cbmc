# A function returning its list parameter returns the same object.
def keep(xs: list[int], extra: int) -> list[int]:
    xs.append(extra)
    return xs


def fresh(xs: list[int]) -> list[int]:
    out = [v for v in xs]
    return out


a = [1]
b = keep(a, 2)
b.append(3)
assert len(a) == 3 and a[2] == 3
c = fresh(a)
c.append(4)
assert len(a) == 3 and len(c) == 4
