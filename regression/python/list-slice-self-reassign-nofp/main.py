# Self-referential slice reassignment preserves ALL elements (was corrupting the
# 2nd element), and reverse-slice too; scalar self-inc unaffected.
xs = [2, 3, 7]
xs = xs[1:4]
assert xs[0] == 3 and xs[1] == 7 and len(xs) == 2
ys = [1, 2, 3]
ys = ys[::-1]
assert ys[0] == 3
a = 5
a = a + 1
assert a == 6
