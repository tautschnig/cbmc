# PLR §6.2.9: 'yield from G()' delegates iteration to G. Each
# value yielded by G is yielded by the enclosing generator.
# Equivalent to:
#     for v in G():
#         yield v
#
# Under the eager-list materialisation model, we evaluate G()
# (which yields a list) and append each element to __gen_result.

def inner():
    yield 1
    yield 2
    yield 3


def outer():
    yield from inner()
    yield 4


def chained():
    yield 0
    yield from inner()
    yield 5


def basic_yield_from() -> None:
    xs = list(outer())
    assert len(xs) == 4
    assert xs[0] == 1
    assert xs[1] == 2
    assert xs[2] == 3
    assert xs[3] == 4


def yield_from_with_extra() -> None:
    xs = list(chained())
    assert len(xs) == 5
    assert xs[0] == 0
    assert xs[1] == 1
    assert xs[2] == 2
    assert xs[3] == 3
    assert xs[4] == 5


basic_yield_from()
yield_from_with_extra()
