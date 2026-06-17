# Read-only access to a replicated list-of-lists stays precise (no spurious
# model bound): all rows hold the same initial values.
grid = [[5, 6]] * 3
assert grid[0][0] == 5
assert grid[1][1] == 6
assert grid[2][0] == 5

# Distinct nested literals are not aliased and may be mutated freely.
d = [[0], [0]]
d[0].append(1)
assert d[0] == [0, 1]
assert d[1] == [0]
