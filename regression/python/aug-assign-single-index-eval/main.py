# PLR §7.2.2: Augmented assignment evaluates the LHS
# expression exactly once. For a[idx()] += ..., the
# index expression must run exactly once — not twice
# (once for the read, once for the write).

a = [0, 0, 0]
i_calls = [0]


def idx() -> int:
    i_calls[0] = i_calls[0] + 1
    return 1


a[idx()] += 10
assert a[1] == 10
assert i_calls[0] == 1
