# pop removes an arbitrary element, and raises KeyError on an empty set.
p = {"a", "b"}
e = p.pop()
assert (e == "a" or e == "b") and len(p) == 1 and e not in p
try:
    set(["z"]).difference({"z"}).pop()
    assert False
except KeyError:
    pass
