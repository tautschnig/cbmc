# The for-loop HEADER-expression checks whole-group: obligations generated
# while converting the iterable / range-argument expressions (subscript
# bounds, tag obligations, conditional exceptions) were silently DROPPED
# (the body-conversion prologue clears pending_checks). `xs[5]` here is out
# of bounds -- CPython raises IndexError before any iteration; both the
# generic-iterable and the range-argument shapes must report it.
def f(xs):
    for c in xs[5]:
        pass
    return 0


def g(xs):
    for i in range(xs[5]):
        pass
    return 0


f([[1], [2], [3]])
g([1, 2, 3])
assert True
