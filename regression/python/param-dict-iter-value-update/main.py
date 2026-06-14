def update_values(d: dict[int, int]) -> None:
    # Iterating a by-reference (parameter) dict must be statically bounded (it
    # never holds more than the model's max entries), and a value-update
    # `d[k] = v` over an iterated key must NOT grow len(d): the iterated
    # dereference is aliased to the object the body mutates, so the key-scan
    # finds the present key in place rather than spuriously appending.
    n0 = len(d)
    for k in d:
        d[k] = 99
    assert len(d) == n0


update_values({1: 10, 2: 20})


def read_only(d: dict[int, int]) -> None:
    n0 = len(d)
    s = 0
    for k in d:
        s += d[k]
    assert len(d) == n0


read_only({1: 10, 2: 20})
