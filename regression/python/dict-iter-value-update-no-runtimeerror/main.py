def f(d: dict[int, int]) -> None:
    # Updating existing keys' values does NOT change the dict size, so it must
    # NOT raise "dictionary changed size during iteration" (a false positive
    # the size-snapshot check must avoid).
    for k in d:
        d[k] = 99
    assert len(d) == 2


f({1: 10, 2: 20})
