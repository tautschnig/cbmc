# PLR 6.2.5 / 4.7: list(d) yields the KEYS in insertion order.
d = {"owner": "strata"}
d["team"] = "moog"
keys = list(d)
assert len(keys) == 2
assert keys[0] == "owner"
assert keys[1] == "team"
assert keys == ["owner", "team"]

e = {}
assert list(e) == []

n = {1: "a", 2: "b"}
ks = list(n)
assert ks[0] == 1 and ks[1] == 2
