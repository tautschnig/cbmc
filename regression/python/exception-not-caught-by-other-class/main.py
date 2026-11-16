def pick(xs: list, i: int) -> int:
    return xs[i]


def safe(xs: list, i: int) -> int:
    try:
        return pick(xs, i)
    except KeyError:         # does not catch IndexError
        return -1


safe([4, 5], 7)
