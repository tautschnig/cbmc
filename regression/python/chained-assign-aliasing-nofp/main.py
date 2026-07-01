# PLR §7.2 no-false-positive guard for chained-assignment aliasing:
#  - a mutation through one chained target IS visible through the other (shared);
#  - an immutable chained assignment binds equal values (no aliasing needed).
a = b = [1]
a.append(2)
assert len(a) == 2 and len(b) == 2

x = y = 5
assert x == 5 and y == 5

d = e = {}
d["k"] = 1
assert len(e) == 1 and e["k"] == 1
