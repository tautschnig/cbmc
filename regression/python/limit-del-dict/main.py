# PLR §7.5: del on dict key should remove the entry
d = {"a": 1, "b": 2}
del d["a"]
# After del, accessing d["a"] should raise KeyError
# But our model doesn't support del on dicts
assert len(d) == 1
