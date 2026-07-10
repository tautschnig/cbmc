# No-false-positive: an unfiltered range(n) comprehension has length exactly n;
# a filtered one has length in [0, n]; range(0) is empty.
xs = [1, 2, 3]
a = [i for i in range(len(xs))]
assert len(a) == 3
b = [i for i in range(len(xs)) if i > 0]
assert len(b) <= 3
