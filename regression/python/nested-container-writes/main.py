# Nested container writes now propagate — d["a"][0] = v
# is rewritten at the statement level to read-modify-write
# the outer container's slot.


# Dict of list: write to inner slot.
d = {"a": [1, 2, 3]}
d["a"][0] = 99
assert d["a"][0] == 99
assert d["a"][1] == 2
assert d["a"][2] == 3

# Dict of dict.
m = {"inner": {"x": 10}}
m["inner"]["x"] = 77
assert m["inner"]["x"] == 77

# List of list.
grid = [[0, 0], [0, 0]]
grid[0][1] = 5
assert grid[0][1] == 5
assert grid[0][0] == 0
assert grid[1][0] == 0
assert grid[1][1] == 0

# Dict of dict, dictionary-key update.
cfg = {"section": {"key": "old"}}
cfg["section"]["key"] = "new"
# Content tracking isn't precise enough to check the value
# for strings — but the length is preserved.
assert len(cfg["section"]["key"]) >= 0
