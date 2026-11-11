# Lookups of literal keys stay precise: a key read from the dict's array
# points to a known constant array, whose content decides the comparison.
d = {"x": 1, "y": 2}
assert d["x"] == 1
assert d["y"] == 2
assert "z" not in d
assert "xy" not in d
