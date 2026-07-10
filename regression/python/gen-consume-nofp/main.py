# No-false-positive: a FRESH inline iterator and a Name-bound generator cursor
# stay PRECISE (only stored-channel re-access degrades to sound nondet).
def g():
    yield 5
    yield 6


assert next(g()) == 5

it = g()
next(it)
assert next(it) == 6
