# The refutation half of fold-shape-soundness: every assert here is
# FALSE in CPython and previously PROVED by an argument-ignoring fold.
# All must fail (or at least not verify).
def stale() -> None:
    assert sum([1, 2, 3], 10) == 6  # start ignored
    assert max([1, -5, 3], key=abs) == 3  # key ignored (CPython: -5)
    assert sorted([1, 2, 3], key=lambda v: -v) == [1, 2, 3]
    assert [1, 2, 1, 2].index(2, 2) == 1  # start ignored (CPython: 3)
    assert "abcabc".startswith("ab", 1)  # pos ignored
    u = {1}.union({2}, {3})
    assert 3 not in u  # variadic args dropped
    ys = [3, 1, 2]
    ys.sort(reverse=True)
    assert ys[0] == 1  # reverse ignored


stale()
