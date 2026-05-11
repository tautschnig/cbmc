# PLR dict.items(): view of (key, value) pairs. For
# dict-literal sources we return a list of python tuples,
# so `for k, v in d.items():` iterates correctly.


d = {"a": 1, "b": 2, "c": 3}
items = d.items()
assert len(items) == 3

# Iteration with tuple unpacking.
total = 0
for k, v in d.items():
    total = total + v
assert total == 6
