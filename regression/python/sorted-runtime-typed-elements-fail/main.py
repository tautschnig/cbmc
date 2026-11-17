def mk(a, b, c):
    out = []
    out.append(a)
    out.append(b)
    out.append(c)
    return out


assert sorted(mk(3, -1, 2)) == [2, -1, 3]
assert sorted(mk(2.5, 1, True)) == [True, 1, 2.5]
assert sorted(mk(1, 5, 3), reverse=True) == [5, 3, 1]
