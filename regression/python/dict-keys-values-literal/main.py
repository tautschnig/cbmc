# dict.keys() / .values() on literal dicts now return
# precise list structs (no member_exprt indirection).


d = {"a": 1, "b": 2, "c": 3}

ks = d.keys()
# len of keys = len of dict
assert len(ks) == 3

vs = d.values()
assert len(vs) == 3

# Sum of values: 1 + 2 + 3 = 6
total = 0
for v in vs:
    total = total + v
assert total == 6
