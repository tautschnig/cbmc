# Higher-order through containers (PLR §6.13): calling a callable
# obtained by subscripting a statically-known container — an inline or
# named list/dict whose elements are functions or lambdas. A constant
# selector folds to one element; a symbolic selector becomes a guarded
# dispatch over the finite element set. Arguments are coerced to the
# callee's parameter types.


def inc(n):
    return n + 1


def dbl(n):
    return n * 2


# Named list of functions, constant index.
fns = [inc, dbl]
assert fns[0](10) == 11
assert fns[1](10) == 20

# Symbolic index, guarded dispatch.
k = 1
assert fns[k](10) == 20

# Inline list.
assert [inc, dbl][0](5) == 6

# Named dict dispatch table.
ops = {"inc": inc, "dbl": dbl}
assert ops["inc"](7) == 8
assert ops["dbl"](7) == 14

# Lambdas in a named list.
gs = [lambda x: x + 100, lambda x: x - 100]
assert gs[0](1) == 101
assert gs[1](1) == -99
