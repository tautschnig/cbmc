# Regression: d.setdefault(key, default) must mutate d to add the
# key when it's missing, so that later containment checks
# (`key in d`) correctly report True.
d: dict[str, int] = {}
d.setdefault("k", 5)
assert "k" in d
assert d["k"] == 5

# When key is already present, setdefault returns the existing value
# and does NOT overwrite.
d2: dict[str, int] = {"a": 1}
v = d2.setdefault("a", 99)
assert v == 1
assert d2["a"] == 1
