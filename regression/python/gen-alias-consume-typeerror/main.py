# PLR §3.1 / §6.2.9: a generator has reference identity, so `it2 = it` binds the
# SAME object and shares its consumption cursor. After `next(it)`, `next(it2)`
# yields the SECOND element. The alias now propagates the generator cursor (the
# alias is a pointer to it's storage; next()/for resolve the shared cursor).
# CPython: x == 2, so this `assert x == 1` raises. Expected: VERIFICATION FAILED.
# (The container `box[0]` and aggregating-builtin list()/sum() channels of the
# same cluster remain KNOWNBUG pending a full generator-object model.)
def g():
    yield 1
    yield 2


it = g()
it2 = it
next(it)
x = next(it2)
assert x == 1
