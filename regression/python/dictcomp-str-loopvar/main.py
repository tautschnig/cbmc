# The iteration variable of a dict comprehension has the type of the current
# element: `x + "v"` concatenates strings rather than raising a TypeError.
d = {x: x + "v" for x in ["k", "m"]}
assert d["k"] == "kv"
assert d["m"] == "mv"
assert len(d) == 2
