# PLR §6.2.9: generator function with two yield points.
# next(g) advances a hidden cursor; the second next(g) must
# return the second yielded value, not the first.
def gen():
    yield 1
    yield 2

g = gen()
x1 = next(g)
x2 = next(g)
assert x1 == 1
assert x2 == 2

# When the eager-yield list is exhausted, next() must raise
# StopIteration so try/except can catch it.
try:
    next(g)
    assert False
except StopIteration:
    pass

# Independent generator instance has its own cursor.
g2 = gen()
y = next(g2)
assert y == 1

# return-before-yield: empty generator. First next() raises.
def empty_gen():
    i = 1
    assert i == 1
    return
    yield 1  # unreachable

ge = empty_gen()
try:
    next(ge)
    assert False
except StopIteration:
    pass

# Generator with yield inside a while loop: cursor walks the
# eager-collected list.
def counted():
    i = 0
    while i < 3:
        yield i
        i += 1

gc = counted()
a = next(gc)
b = next(gc)
c = next(gc)
assert a == 0
assert b == 1
assert c == 2
