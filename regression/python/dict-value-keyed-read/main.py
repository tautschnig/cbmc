# Heterogeneous-key dicts are stored as dict[value, value]; reading
# them back and membership tests must match keys in the value domain
# (string content for str keys, payload otherwise) rather than via
# declared-type-gated equality. Regression for value_equal.
d = {1: 10, "b": 20, 3.5: 30}
assert d[1] == 10
assert d["b"] == 20
assert d[3.5] == 30

# Membership ('in') must use the same value-domain comparison.
assert "b" in d
assert 1 in d
assert "z" not in d
assert 99 not in d

# A missing key must still raise KeyError (no spurious match against
# another key's unused payload slot).
caught = False
try:
    _ = d[0]
except KeyError:
    caught = True
assert caught
