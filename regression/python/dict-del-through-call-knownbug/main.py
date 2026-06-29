# KNOWNBUG (false proof, c2): `del p["x"]` inside a function does not propagate
# the deletion to the caller's dict. Dicts are by-reference for EXISTING-key
# value modifications but NOT for STRUCTURAL mutations (adding/deleting a key),
# so the keys/length change does not cross the call boundary. CPython: the later
# d["x"] read raises KeyError. Desired: VERIFICATION FAILED.
d = {"x": 1, "y": 2}


def rm(p) -> None:
    del p["x"]


rm(d)
v = d["x"]   # KeyError in CPython (x was deleted through the call)
