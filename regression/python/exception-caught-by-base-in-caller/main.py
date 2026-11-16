def pick(xs: list, i: int) -> int:
    return xs[i]


def safe(xs: list, i: int) -> int:
    try:
        return pick(xs, i)
    except LookupError:      # catches the IndexError raised in pick
        return -1


assert safe([4, 5], 1) == 5
assert safe([4, 5], 7) == -1
