# PLR §4.10: dict.get(key, default)
d = {"a": 1, "b": 2}
assert d.get("a", 0) == 1
assert d.get("b", 0) == 2
