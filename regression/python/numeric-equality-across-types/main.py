def mk(a, b):
    out = []
    out.append(a)
    out.append(b)
    return out


xs = mk(1, 2.0)
assert xs[0] == True
assert xs == [True, 2]
assert mk(True, 1) == [1, 1]
