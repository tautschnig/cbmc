# PLR §6.10.1 + §6.2.4: list slicing must zero-fill positions
# beyond the resulting length. Without this, the slice's data
# array reads from the source list past the slice end, leaving
# stale values that don't match a list literal's trailing zeros
# at struct-equality time.

def basic_slice() -> None:
    xs = [1, 2, 3, 4, 5]
    ys = xs[1:4]
    assert ys == [2, 3, 4]


def empty_slice() -> None:
    xs = [1, 2, 3]
    ys = xs[3:3]
    assert ys == []


def slice_to_end() -> None:
    xs = [1, 2, 3, 4]
    ys = xs[2:]
    assert ys == [3, 4]


def slice_from_start() -> None:
    xs = [1, 2, 3, 4]
    ys = xs[:2]
    assert ys == [1, 2]


basic_slice()
empty_slice()
slice_to_end()
slice_from_start()
