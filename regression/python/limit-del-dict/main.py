# PLR §7.5: del on dict key zeros the value
d = {"a": 1, "b": 2}
del d["a"]
assert d["a"] == 0  # value zeroed after del
assert d["b"] == 2  # other keys unaffected
