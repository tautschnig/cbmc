# KNOWNBUG (generator consumption-state cluster). A generator's eager
# list-with-cursor model does not let a `for` loop respect the cursor: after
# `next(it)` consumes the first yield, `for x in it` iterates the WHOLE eager
# list from the start (the for-loop counter is cursor-independent) instead of
# resuming after the consumed element. CPython: the loop yields 2 and 3, so
# total == 5; this `assert total == 6` raises AssertionError. cbmc iterates
# 1+2+3 == 6 and proves the assertion (false proof). Desired: VERIFICATION
# FAILED. Needs generator-object consumption state shared across access paths.
def g():
    yield 1
    yield 2
    yield 3


it = g()
next(it)
total = 0
for x in it:
    total += x
assert total == 6
