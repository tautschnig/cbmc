# PLR §6.2.9: an aggregating builtin over a generator consumes only its REMAINING
# items. After a partial next(), list(it)/sum(it) start from the cursor (were
# re-yielding from 0 -- a false proof). The cursor is Name-resolvable so list/sum
# share the consume-from-cursor helper.
def g():
    yield 1
    yield 2
    yield 3


it = g()
next(it)
assert list(it) == [2, 3]
assert len(list(g())) == 3  # fresh generator: full

it2 = g()
next(it2)
assert sum(it2) == 5  # 2 + 3

it3 = g()
next(it3)
next(it3)
assert list(it3) == [3]
