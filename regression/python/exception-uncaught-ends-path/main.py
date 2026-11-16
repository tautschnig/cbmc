log = []


def pick(xs: list, i: int) -> int:
    v = xs[i]          # IndexError for i == 3: nothing can catch it
    log.append(v)
    return v


n = nondet_int()
assume(0 <= n <= 3)
pick([4, 5, 6], n)
assert len(log) == 1   # holds on every path that gets here
