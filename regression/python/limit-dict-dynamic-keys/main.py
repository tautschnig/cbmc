# PLR §3.2: Dicts should support dynamic key insertion
# Current model: struct-per-key (only literal keys at creation time)
d = {}
d["x"] = 1
d["y"] = 2
assert d["x"] == 1
assert len(d) == 2
del d["x"]
assert len(d) == 1
