# Tier 1: straight-line type change — solvable by fresh variable renaming
x = 5
assert x == 5
x = "hello"
assert len(x) == 5
