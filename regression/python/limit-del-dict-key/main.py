# PLR §7.5: del d["key"] should remove key from dict
d: dict = {"a": 1, "b": 2}
del d["a"]
assert "a" not in d
