d = {}
d["k"] = "v"
e = d.get("other")
assert e is None                       # part 1 (still holds)
assert not isinstance(e, str)          # part 2 CORRECTED (None is not a str)
f = d.get("k")
assert isinstance(f, str)              # a REAL str still matches
assert not isinstance(None, str)
x = 5
assert isinstance(x, int)
