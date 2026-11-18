# The assertion fails in CPython.
# An empty set of ints (set()) was truthy, so `if set():` was taken.
x = 0
if set():
    x = 1
assert x == 1, "set_truth"
