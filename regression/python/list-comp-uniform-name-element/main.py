N = 4


def credits(fill: int) -> int:
    start = fill
    pool = [start for _ in range(N)]
    return pool[0] + pool[3]


assert credits(2) == 4
assert credits(0) == 0
