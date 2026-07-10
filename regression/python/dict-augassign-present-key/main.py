# A present key augments normally (no spurious KeyError).
d = {0: 9}
d[0] += 5
assert d[0] == 14
