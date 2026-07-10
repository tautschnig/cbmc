# CORE (PLR §6.2.9): a generator is a reference; passing it to a function that
# consumes it advances the shared cursor, so the caller sees the consumption.
# The list-with-cursor model passes the backing list by value (independent
# cursor), which previously let the caller re-yield from the start -- a false
# proof (`c(it)` consumed the first element, yet `next(it) == 1` was proved).
# Now the caller's cursor is soundly havoc'd to an unknown position after the
# generator is passed to a user function, so the re-yield can no longer be
# proved. CPython: after c(it) consumes element 1, next(it) is 2, so
# `next(it) == 1` raises AssertionError -> VERIFICATION FAILED.
def g():
    yield 1
    yield 2
    yield 3


def c(it):
    return next(it)


it = g()
c(it)
assert next(it) == 1
