# No-false-positive: self-assignment preserves the object; real aliasing to a
# DIFFERENT name must still share identity (mutation visible via the source).
xs = [1, 7, 7]
xs = xs
assert len(xs) == 3 and xs[0] == 1
d = {0: 1}
d = d
assert d[0] == 1
a = [1, 2, 3]
b = a
b[0] = 99
assert a[0] == 99
