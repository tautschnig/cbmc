# Witness that a partially-consumed generator does NOT re-yield consumed items:
# asserting the WRONG (full) result must FAIL (CPython raises AssertionError).
def g():
    yield 1
    yield 2
    yield 3


it = g()
next(it)
assert list(it) == [1, 2, 3]
