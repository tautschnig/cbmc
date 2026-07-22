# PLR §6.7 under --python-unbounded-ints (mathematical integers): Python
# modulo takes the DIVISOR's sign and floor division rounds toward
# -infinity; SMT-LIB Int division is EUCLIDEAN (remainder always >= 0).
# The integer lowering previously fell through to NONDET for `%` (every
# mod unconstrained -- spurious failures and both-ways branching) and to
# Euclidean division for `//` (7 // -2 gave -3 instead of -4 -- a VALUE
# miscomputation and false-proof vector). Both are now constructed from
# the Euclidean remainder: r_f = (b < 0 && r_e != 0) ? r_e + b : r_e;
# q_f = (a - r_f) / b (exact division agrees across semantics). All four
# sign quadrants pinned, plus the symbolic-argument shape.
assert 7 % 3 == 1
assert -7 % 3 == 2
assert 7 % -3 == -2
assert -7 % -3 == -1
assert 7 // 2 == 3
assert -7 // 2 == -4
assert 7 // -2 == -4
assert -7 // -2 == 3


def f(a: int, b: int) -> int:
    return a % b


assert f(-7, 3) == 2
assert f(10 ** 20 + 7, 3) == (10 ** 20 + 7) % 3
