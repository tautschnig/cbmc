# Twin: after b.append(2), a has 2 elements.
def same(xs: list[int]) -> list[int]:
    return xs


a = [1]
b = same(a)
b.append(2)
assert len(a) == 1
