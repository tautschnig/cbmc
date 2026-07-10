# Precision: after a mutation, the fold-based ops read the RUNTIME (mutated) list.
xs = [4, 0]
xs.append(-5)
assert min(xs) == -5
ys = [6, 2]
ys.append(2)
assert sorted(ys) == [2, 2, 6]
zs = [1, 9, 2]
zs.pop(1)
assert max(zs) == 2
