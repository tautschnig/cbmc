# Symbol-element caches are kept while valid (no reassignment); reassignment
# invalidates precisely, and the value before reassignment is correct.
b = 7
xs = [b, 2]
assert xs == [7, 2]
c = 0
ys = [10] + [c]
assert ys == [10, 0]
c = 5
assert ys == [10, 0]
