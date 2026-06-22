# PLR §6.4 + §3.1: a mutable list/dict stored as a dict value is mutated in
# place; the change must be observed on later reads. dict subscript-read and
# setdefault return the owning dict's values[] lvalue slot (Option 2), so
# each dict owns its storage (no cross-dict aliasing).

# Direct subscript mutation.
a = {1: []}
a[1].append(5)
assert len(a[1]) == 1
assert a[1][0] == 5

a[1].append(6)
assert len(a[1]) == 2

# setdefault returning an existing mutable value.
b = {1: [0]}
b.setdefault(1, []).append(9)
assert len(b[1]) == 2

# Two distinct dicts must NOT alias.
c = {7: []}
d = {7: []}
c[7].append(1)
assert len(c[7]) == 1
assert len(d[7]) == 0

# String-keyed dict with list value.
e = {"k": [1]}
e["k"].append(2)
assert len(e["k"]) == 2
