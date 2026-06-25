# PLR object identity: a list whose elements are themselves mutable containers
# holds those elements BY REFERENCE. Replication (`[x] * n`), extraction
# (`row = grid[i]`) and double-subscript writes (`grid[i][j] = v`) all share the
# SAME inner object, so an in-place mutation through any alias is visible
# through all of them. Reference semantics (the default; see
# doc/python-frontend-reference-semantics-spike.md) models this PRECISELY --
# this case used to be a sound-but-imprecise `python-model-bound` cut and is now
# verified exactly. The genuinely-false direction (asserting independence) is
# covered by ref-mutables-sound-neg.

# Replication shares one inner list; a double-subscript write is visible
# through every row.
grid = [[0, 0]] * 3
grid[0][0] = 1
assert grid[1][0] == 1
assert grid[2][0] == 1

# Element-extraction alias: `row` is the SAME shared inner object; appending
# through it is visible through every row.
grid2 = [[0]] * 3
row = grid2[0]
row.append(9)
assert grid2[1] == [0, 9]
assert grid2[2] == [0, 9]
