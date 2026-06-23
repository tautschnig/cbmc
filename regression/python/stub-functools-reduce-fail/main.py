import functools
import operator
# was a false proof: reduce returned None; CPython returns 6.
assert functools.reduce(operator.add, [1, 2, 3], 0) is None
