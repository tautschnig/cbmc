# PLR dict.update(other): merge entries from other into d.
# Existing keys are replaced; new keys are appended.


d = {"a": 1, "b": 2}
d.update({"b": 20, "c": 30})
assert d["a"] == 1  # unchanged
assert d["b"] == 20  # replaced
assert d["c"] == 30  # appended
assert len(d) == 3

# Update on empty dict
e = {}
e.update({"x": 100, "y": 200})
assert e["x"] == 100
assert e["y"] == 200

# Update with empty dict — no change
f = {"k": 5}
f.update({})
assert f["k"] == 5
assert len(f) == 1
