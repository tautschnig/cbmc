bins = [[], [], []]


def put(k: int, v: int) -> None:
    bins[k].append(v)


put(2, 7)
put(2, 9)
assert len(bins[2]) == 2
assert bins[2][1] == 7
assert len(bins[0]) == 0
