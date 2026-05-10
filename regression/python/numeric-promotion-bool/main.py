# Regression: numeric type promotion across bool, int widths.

# bool + int: widen bool to int.
assert True + 3 == 4
assert False + 3 == 3

# bool - int, bool * int, bool / int.
assert True - 1 == 0
assert True * 7 == 7

# Comparisons across types.
assert True == 1
assert False == 0


def add_one(x: int) -> int:
    return x + 1


# Mixed-width int args (all int64 in our frontend, but the
# promotion logic now also handles it symmetrically).
assert add_one(True) == 2
assert add_one(5) == 6
