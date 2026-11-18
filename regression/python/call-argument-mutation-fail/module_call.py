# The assertion fails in CPython.
# A list passed to a library function called through its module
# (bisect.insort_right) lost the function's changes.
import bisect

xs = [1, 3]
bisect.insort_right(xs, 2)
assert len(xs) == 2, "module_call"
