# PLR §6.4: dict.fromkeys(iterable[, value]) and dict-literal key dedup.
# A dict has at most one entry per key (last value wins).

d = dict.fromkeys([1, 2, 3], 7)
assert len(d) == 3
assert d[1] == 7 and d[2] == 7 and d[3] == 7

# Duplicate keys de-duplicate (fromkeys).
dup = dict.fromkeys([1, 1, 2, 3, 3], 5)
assert len(dup) == 3

# Float fill value.
f = dict.fromkeys([1, 2, 3], 4.0)
assert f[2] == 4.0

# Duplicate keys in a dict literal also de-duplicate (last wins).
lit = {1: 10, 1: 20, 2: 30}
assert len(lit) == 2
assert lit[1] == 20
