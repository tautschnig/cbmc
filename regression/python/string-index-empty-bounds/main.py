# PLR / CPython str.index/find with an EMPTY substring: found at `start`
# only when start <= len and start <= end; inverted/out-of-range bounds
# raise ValueError (index) / return -1 (find). The index/rindex ValueError
# is now properly TYPED so `except ValueError` catches it.
got = False
try:
    "abc".index("", 2, 1)  # inverted -> ValueError
except ValueError:
    got = True
assert got

got2 = False
try:
    "abc".index("z")  # missing -> ValueError (typed)
except ValueError:
    got2 = True
assert got2

# valid empty-substring results
assert "abc".index("") == 0
assert "abc".index("", 3, 3) == 3
assert "abc".find("", 2, 1) == -1
assert "abc".find("", 0, 3) == 0
assert "abc".rfind("") == 3
assert "abc".rfind("", 2, 1) == -1
