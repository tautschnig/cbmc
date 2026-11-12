# The values of a dict comprehension over strings are exact.
d = {x: x + "v" for x in ["k", "m"]}
assert d["m"] == "kv"
