# No-false-positive guard for the non-iterable whole-group: iterating/unpacking
# a genuine iterable -- or an Any-typed param (MIGHT be iterable) -- must NOT be
# flagged. Covers for-loop, unpack, comprehension over list / str / range / tuple
# and an Any parameter.
def use(x) -> int:
    total = 0
    for i in x:  # Any param: not flagged
        total += 1
    return total


assert use([1, 2, 3]) == 3
a, b = [10, 20]
assert a == 10
c, d = "xy"
assert d == "y"
e, f = (1, 2)
assert f == 2
r = [z for z in range(4)]
assert len(r) == 4
for ch in "ab":
    pass
