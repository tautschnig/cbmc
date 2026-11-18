# The assertion fails in CPython.
# A call that is not modelled left the list it was passed unchanged.
import os

xs = ["a"]
try:
    os.not_a_function(xs)
except AttributeError:
    xs.append("b")
assert len(xs) == 1, "unmodelled"
