# No-false-positive guard: homogeneous lists, the numeric tower (int/float),
# a key= remap, and same-category (all str / all list) must NOT be flagged.
assert min([3, 1, 2]) == 1
assert max(["a", "b"]) == "b"
assert min([1, 2.5]) == 1
assert min([[1, 2], [0, 9]], key=lambda p: p[0]) == [0, 9]
assert sorted([3, 1, 2]) == [1, 2, 3]
