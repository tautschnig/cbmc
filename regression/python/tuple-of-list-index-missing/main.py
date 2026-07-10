# tuple(xs).index(v) with absent v raises ValueError (was silently succeeding,
# a false proof, because tuple(xs) was an untyped nondet).
xs = [1, 2]
t = tuple(xs)
r = t.index(9)
