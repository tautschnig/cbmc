# Nested container access — reads work precisely for
# dict-literal / list-literal trees. Nested writes
# (d["a"][0] = v) have a known limitation because the
# inner read returns a copy; writing into the copy does
# not propagate back. This test covers the read cases
# that do work.


# Nested dict access
d = {"a": {"b": 42}}
assert d["a"]["b"] == 42

# Nested list access
m = [[1, 2], [3, 4]]
assert m[0][0] == 1
assert m[0][1] == 2
assert m[1][0] == 3
assert m[1][1] == 4

# Dict of lists
mm = {"xs": [10, 20, 30]}
assert mm["xs"][0] == 10
assert mm["xs"][2] == 30

# List of dicts
rows = [{"n": 1}, {"n": 2}, {"n": 3}]
assert rows[0]["n"] == 1
assert rows[2]["n"] == 3
