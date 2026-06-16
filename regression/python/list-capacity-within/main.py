# Within-capacity list growth stays precise (and the python-model-bound guard
# at each producer is discharged): repetition, concatenation, extend.
a = [0] * 10
assert len(a) == 10

b = [1, 2, 3] + [4, 5, 6]
assert len(b) == 6
assert b[4] == 5

c = [1, 2, 3]
c.extend([4, 5, 6])
assert len(c) == 6
assert c[4] == 5
