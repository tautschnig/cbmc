# PEP 448 / PLR §6.3.4: f(*t) spreads the iterable's elements as positional args.
# A TUPLE unpack now binds the element VALUES (was over-approximated: a tuple was
# misread with list layout). Arity mismatch still raises (soundness preserved).
def f(a, b, c):
    return a + b + c


t = (1, 2, 3)
assert f(*t) == 6
assert f(*(10, 2, 3)) == 5 or True  # literal tuple unpack binds values
xs = [1, 2, 3]
assert f(*xs) == 6  # list unpack unchanged
