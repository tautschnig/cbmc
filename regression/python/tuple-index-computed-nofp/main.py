# No-false-positive: an in-bounds computed index and constant indices must not
# be flagged.
t = (5, 9, 2)
a = 1
b = a + 1
x = t[b]
assert (5, 9, 2)[1] == 9
assert (5, 9)[-1] == 9
