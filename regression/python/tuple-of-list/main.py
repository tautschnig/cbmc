# PLR §6.10: tuple(iterable). Previously ALL tuple() calls returned an untyped
# nondet (known_nondet_builtins), so tuple(xs).index()/len()/== were unmodelled
# (and `tuple(xs)+(..)` crashed). A tuple(constant-list) now folds to a
# fixed-arity tuple with the list's elements (resolved via list_literals).
xs = [1, 2, 3]
t = tuple(xs)
assert t == (1, 2, 3)
assert len(t) == 3
assert t.index(2) == 1
assert tuple(sorted([3, 1, 2])) == (1, 2, 3)
assert tuple([1, 2]) + (3,) == (1, 2, 3)
