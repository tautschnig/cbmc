s = ""
for w in ["a", "bb", "ccc", "dddd"]:
    s = s + w
assert len(s) == 10
x = nondet_str()
t = s + x
assert len(t) >= 10
