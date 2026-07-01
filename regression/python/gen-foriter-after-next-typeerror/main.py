# PLR §6.2.9: iterating a generator consumes from its CURRENT position. After
# `next(it)` consumes the first yield, `for x in it` must resume AFTER it (yield
# 2 and 3, total 5), not re-iterate from the start. The for-loop now starts at
# the generator's cursor and exhausts it. CPython: total == 5, so this
# `assert total == 6` raises AssertionError. Expected: VERIFICATION FAILED.
# (Closed 2026-07-01; the alias and container channels of the same cluster
# remain KNOWNBUG pending a full generator-object model.)
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
