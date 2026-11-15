SLOTS = 3
bins = [[] for _ in range(SLOTS)]


def put(k: int, v: int) -> None:
    bins[k].append(v)


def total() -> int:
    t = 0
    for k in range(SLOTS):
        while bins[k]:
            t += bins[k].pop(0)
    return t


put(0, 4)
put(2, 5)
assert total() == 9
