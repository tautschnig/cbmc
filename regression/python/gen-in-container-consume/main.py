# PLR §6.2.9: a generator stored in a container and consumed via the slot
# (`box=[g()]; next(box[0]); next(box[0])`) advances the SAME object, so the 2nd
# read yields 2 -- `assert x == 1` raises in CPython. cbmc has no per-Name cursor
# for a stored channel, so it soundly models the yielded value as nondet (rather
# than the unsound first-yield guess) -> cannot prove x == 1 -> VERIFICATION
# FAILED. Was a KNOWNBUG false proof.
def g():
    yield 1
    yield 2


box = [g()]
next(box[0])
x = next(box[0])
assert x == 1
