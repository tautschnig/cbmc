# KNOWNBUG (false proof): binary-operator evaluation-order MIRROR case -- a
# side-effecting RIGHT operand mutates a value read by the LEFT operand. CPython
# evaluates operands left-to-right: it reads x (=1) BEFORE calling g() (which
# sets x=100), so r = 1 + 0 = 1. cbmc hoists g() before the binop and reads x
# AFTER it (=100), so r = 100; asserting the cbmc value wrongly verifies.
#
# The FORWARD case (`g() + x`, a left-call mutating a right-read) is fixed
# (convert_bin_op materialises a side-effecting left operand before the right
# read). This REVERSE case needs materialising the LEFT operand's READ into a
# temp before evaluating the RIGHT operand. Deferred: applying full left-to-right
# operand materialisation broadly regressed recursive-unwinding convergence
# (recursion2 / --incremental-bmc), so it needs a narrowly-gated approach
# (e.g. only when the right operand is a side-effecting call and the left is a
# read of a global/attribute it could mutate). Rarer than the forward case.
x = 1


def g() -> int:
    global x
    x = 100
    return 0


r = x + g()
assert r == 100   # FALSE in CPython (r is 1; x is read before g() runs)
