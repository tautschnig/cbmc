# No-false-positive: the concat-fold preserves content, length, and index; mixed
# element types are promoted without crashing; homogeneous min is exact.
assert ([1, 2] + [3]) == [1, 2, 3]
assert len([1, 2] + [3, 4]) == 4
assert ([1, 2] + [3])[2] == 3
assert len([1] + ["a"]) == 2
xs = [3, 1]
xs = xs + [0]
assert min(xs) == 0
