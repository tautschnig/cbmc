s = ""
for w in ["a", "bb", "ccc", "dddd"]:
    s = s + w
x = nondet_str()
t = s + x
assert len(t) != 13
