# Precision: subscript/aug store then fold reads the runtime (mutated) list.
xs = [1, 2]
xs[0] = 9
assert min(xs) == 2
ys = [1, 2]
ys[0] += 8
assert min(ys) == 2
