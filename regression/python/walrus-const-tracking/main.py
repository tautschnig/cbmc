# Precision kept: a walrus with a constant value tracks it, so the subscript folds.
r = (b := 2)
t = (6, 3, 9)
assert t[b] == 9
assert r == 2
