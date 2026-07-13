# CORE no-false-alarm: a default reading an already-bound module global must NOT
# false-alarm -- def-time evaluation happens in SOURCE ORDER, after G's
# assignment. G[1] is in-bounds, so no IndexError.
G = [10, 20]


def f(a=G[1]):
    return a


assert f() == 20
