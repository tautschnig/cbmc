# PLR §7.5: del on dict key removes the entry
d = {"a": 1, "b": 2}
del d["a"]
assert d["b"] == 2  # other keys unaffected
