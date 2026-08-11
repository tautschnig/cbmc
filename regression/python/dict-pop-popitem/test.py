# Regression: dict.pop(key) and dict.popitem() must mutate the dict
# (decrementing length and shifting/removing entries) and raise
# KeyError on missing key / empty dict.

# pop(key) on present key returns and removes the value.
d: dict[str, int] = {"a": 1, "b": 2}
v = d.pop("a")
assert v == 1
assert "a" not in d
assert d["b"] == 2

# pop(key, default) on missing key returns default without mutation.
e: dict[str, int] = {"x": 9}
w = e.pop("missing", 42)
assert w == 42
assert e["x"] == 9

# popitem() on non-empty dict returns the LIFO (key, value) and
# decrements length.
f: dict[str, int] = {"k": 7}
k, v2 = f.popitem()
assert k == "k"
assert v2 == 7
