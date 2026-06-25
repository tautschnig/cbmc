# Int-keyed dict values use a writable lvalue slot: in-place mutation
# propagates and stays precise (not havoced).
d = {1: [10]}
d[1].append(20)
assert len(d[1]) == 2
assert d[1][1] == 20
