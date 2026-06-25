# Reference-semantics soundness: `[x] * n` shares ONE inner object (PLR object
# identity), so a mutation through one row IS visible through the others.
# Asserting the rows are independent is therefore FALSE and must produce
# VERIFICATION FAILED -- the by-value model used to risk a false proof here and
# cut the path with python-model-bound; reference semantics models the aliasing
# precisely, so the false claim genuinely fails.
grid = [[0]] * 3
grid[0].append(9)
assert grid[1] == [0]
