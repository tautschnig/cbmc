# KNOWNBUG (generator consumption-state cluster). A generator object has
# reference identity: `it2 = it` binds the SAME object, so consuming through one
# alias advances the other. The eager list-with-cursor model allocates the
# cursor against the original Name only, and an alias gets an independent
# (fresh) view. CPython: after `next(it)`, `next(it2)` yields 2, so x == 2 and
# this `assert x == 1` raises. cbmc treats it2 as fresh and returns 1, proving
# the assertion (false proof). Desired: VERIFICATION FAILED. Needs
# generator-object identity (shared consumption state across aliases).
def g():
    yield 1
    yield 2


it = g()
it2 = it
next(it)
x = next(it2)
assert x == 1
