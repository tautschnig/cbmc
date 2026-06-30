# KNOWNBUG (generator consumption-state cluster). A generator stored in a
# container (`box = [g()]`) is consumed via `box[0]`. The cursor is keyed on the
# original call-site binding, not the container slot, so each `next(box[0])`
# reads from a fresh view. CPython: after the first `next(box[0])`, the second
# yields 2, so x == 2 and this `assert x == 1` raises. cbmc returns 1 and proves
# the assertion (false proof). Desired: VERIFICATION FAILED. Needs
# generator-object identity (consumption state tied to the object, not the Name).
def g():
    yield 1
    yield 2


box = [g()]
next(box[0])
x = next(box[0])
assert x == 1
